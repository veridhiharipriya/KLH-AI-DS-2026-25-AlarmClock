#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_ALARMS 64
#define LABEL_SIZE 80
#define TIMEZONE_SIZE 64
#define REQUEST_SIZE 16384
#define RESPONSE_SIZE 32768

typedef struct {
    int id;
    int enabled;
    unsigned int repeat_days;
    time_t when;
    time_t snooze_until;
    char timezone[TIMEZONE_SIZE];
    char label[LABEL_SIZE];
} Alarm;

typedef struct {
    sem_t mutex;
    int next_id;
    Alarm alarms[MAX_ALARMS];
} SharedState;

typedef struct {
    int id;
    time_t when;
    char label[LABEL_SIZE];
} FiredAlarm;

static volatile sig_atomic_t scheduler_tick = 0;
static volatile sig_atomic_t keep_running = 1;
static char data_dir[512] = "data";

static void on_alarm(int signal_number) {
    (void)signal_number;
    scheduler_tick = 1;
}

static void on_stop(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static void make_path(char *out, size_t size, const char *name) {
    snprintf(out, size, "%s/%s", data_dir, name);
}

static void lock_state(SharedState *state) {
    while (sem_wait(&state->mutex) == -1 && errno == EINTR) {
    }
}

static void unlock_state(SharedState *state) {
    sem_post(&state->mutex);
}

static bool valid_timezone_name(const char *timezone) {
    if (!timezone[0] || strlen(timezone) >= TIMEZONE_SIZE ||
        strstr(timezone, "..")) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)timezone; *p; p++) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' ||
              *p == '+' || *p == '/')) {
            return false;
        }
    }
    char zoneinfo_path[128];
    int count = snprintf(zoneinfo_path, sizeof(zoneinfo_path),
                         "/usr/share/zoneinfo/%s", timezone);
    struct stat status;
    return count > 0 && (size_t)count < sizeof(zoneinfo_path) &&
           stat(zoneinfo_path, &status) == 0 && S_ISREG(status.st_mode);
}

static time_t next_weekly_occurrence(time_t occurrence, time_t after,
                                     unsigned int days, const char *timezone) {
    if (!days) {
        return occurrence;
    }

    const char *previous_timezone = getenv("TZ");
    char *saved_timezone =
        previous_timezone ? strdup(previous_timezone) : NULL;
    if (previous_timezone && !saved_timezone) {
        perror("Could not save process timezone");
        return occurrence;
    }
    if (timezone[0] && setenv("TZ", timezone, 1) == -1) {
        perror("Could not set alarm timezone");
        free(saved_timezone);
        return occurrence;
    }
    tzset();

    struct tm calendar_time;
    struct tm current_time;
    if (!localtime_r(&occurrence, &calendar_time) ||
        !localtime_r(&after, &current_time)) {
        if (saved_timezone) {
            setenv("TZ", saved_timezone, 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
        free(saved_timezone);
        return occurrence;
    }

    calendar_time.tm_year = current_time.tm_year;
    calendar_time.tm_mon = current_time.tm_mon;
    calendar_time.tm_mday = current_time.tm_mday;
    time_t next = occurrence;
    for (int day = 0; day <= 7; day++) {
        calendar_time.tm_isdst = -1;
        time_t candidate = mktime(&calendar_time);
        if (candidate == (time_t)-1 ||
            !localtime_r(&candidate, &calendar_time)) {
            break;
        }
        if (candidate > after && (days & (1U << calendar_time.tm_wday))) {
            next = candidate;
            break;
        }
        calendar_time.tm_mday++;
    }

    if (saved_timezone) {
        if (setenv("TZ", saved_timezone, 1) == -1) {
            perror("Could not restore process timezone");
        }
    } else if (unsetenv("TZ") == -1) {
        perror("Could not restore process timezone");
    }
    tzset();
    free(saved_timezone);
    return next;
}

static void load_alarms(SharedState *state) {
    char path[600];
    make_path(path, sizeof(path), "alarms.tsv");
    FILE *file = fopen(path, "r");
    if (!file) {
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        Alarm alarm = {0};
        long long timestamp = 0;
        long long snooze_until = 0;
        char label[LABEL_SIZE] = {0};
        char timezone[TIMEZONE_SIZE] = {0};
        int repeat_days = 0;
        int fields = sscanf(line, "%d\t%lld\t%d\t%d\t%lld\t%63[^\t]\t%79[^\n]",
                            &alarm.id, &timestamp, &alarm.enabled, &repeat_days,
                            &snooze_until, timezone, label);
        if (fields != 7) {
            memset(&alarm, 0, sizeof(alarm));
            memset(label, 0, sizeof(label));
            if (sscanf(line, "%d\t%lld\t%d\t%79[^\n]", &alarm.id,
                       &timestamp, &alarm.enabled, label) < 3) {
                continue;
            }
        } else {
            alarm.repeat_days = (unsigned int)repeat_days & 0x7fU;
            alarm.snooze_until = (time_t)snooze_until;
            snprintf(alarm.timezone, sizeof(alarm.timezone), "%s", timezone);
        }
        if (alarm.id <= 0) {
            continue;
        }
        alarm.when = (time_t)timestamp;
        snprintf(alarm.label, sizeof(alarm.label), "%s", label);
        for (int i = 0; i < MAX_ALARMS; i++) {
            if (!state->alarms[i].id) {
                state->alarms[i] = alarm;
                if (alarm.id >= state->next_id) {
                    state->next_id = alarm.id + 1;
                }
                break;
            }
        }
    }
    fclose(file);
}

static void save_alarms(SharedState *state) {
    char path[600];
    make_path(path, sizeof(path), "alarms.tsv");
    FILE *file = fopen(path, "w");
    if (!file) {
        perror("Could not save alarms");
        return;
    }

    lock_state(state);
    for (int i = 0; i < MAX_ALARMS; i++) {
        Alarm *alarm = &state->alarms[i];
        if (alarm->id) {
            fprintf(file, "%d\t%lld\t%d\t%u\t%lld\t%s\t%s\n", alarm->id,
                    (long long)alarm->when, alarm->enabled, alarm->repeat_days,
                    (long long)alarm->snooze_until,
                    alarm->timezone[0] ? alarm->timezone : "UTC", alarm->label);
        }
    }
    unlock_state(state);
    fclose(file);
}

static void append_json_string(char *out, size_t size, const char *value) {
    size_t used = strlen(out);
    if (used >= size) {
        return;
    }
    out[used++] = '"';
    out[used] = '\0';
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        char escaped[7];
        const char *text = escaped;
        size_t length = 0;
        if (*p == '"' || *p == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)*p;
            length = 2;
        } else if (*p < 0x20) {
            snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
            length = 6;
        } else {
            escaped[0] = (char)*p;
            length = 1;
        }
        if (used + length + 2 >= size) {
            break;
        }
        memcpy(out + used, text, length);
        used += length;
    }
    out[used++] = '"';
    out[used] = '\0';
}

static void appendf(char *out, size_t size, const char *format, ...) {
    size_t used = strlen(out);
    if (used >= size) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(out + used, size - used, format, args);
    va_end(args);
}

static void append_history_json(char *json, size_t size) {
    char path[600];
    make_path(path, sizeof(path), "history.tsv");
    FILE *file = fopen(path, "r");
    appendf(json, size, "\"history\":[");
    if (!file) {
        appendf(json, size, "],");
        return;
    }

    typedef struct {
        long long when;
        int id;
        char label[LABEL_SIZE];
    } HistoryRow;
    HistoryRow rows[20] = {0};
    size_t count = 0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        HistoryRow row = {0};
        if (sscanf(line, "%lld\t%d\t%79[^\n]", &row.when, &row.id,
                   row.label) != 3) {
            continue;
        }
        rows[count % 20] = row;
        count++;
    }
    fclose(file);
    size_t shown = count < 20 ? count : 20;
    size_t start = count > 20 ? count % 20 : 0;
    for (size_t i = 0; i < shown; i++) {
        HistoryRow *row = &rows[(start + i) % 20];
        if (i) {
            appendf(json, size, ",");
        }
        appendf(json, size, "{\"id\":%d,\"when\":%lld,\"label\":",
                row->id, row->when);
        append_json_string(json, size, row->label);
        appendf(json, size, "}");
    }
    appendf(json, size, "],");
}

static void build_state_json(SharedState *state, char *json, size_t size) {
    json[0] = '\0';
    appendf(json, size, "{\"now\":%lld,\"alarms\":[",
            (long long)time(NULL));
    lock_state(state);
    bool first = true;
    for (int i = 0; i < MAX_ALARMS; i++) {
        Alarm *alarm = &state->alarms[i];
        if (!alarm->id) {
            continue;
        }
        if (!first) {
            appendf(json, size, ",");
        }
        first = false;
        appendf(json, size,
                "{\"id\":%d,\"when\":%lld,\"enabled\":%s,"
                "\"repeatDays\":%u,\"snoozeUntil\":%lld,\"label\":",
                alarm->id, (long long)alarm->when,
                alarm->enabled ? "true" : "false", alarm->repeat_days,
                (long long)alarm->snooze_until);
        append_json_string(json, size, alarm->label);
        appendf(json, size, "}");
    }
    unlock_state(state);
    appendf(json, size, "],");
    append_history_json(json, size);
    size_t used = strlen(json);
    if (used && json[used - 1] == ',') {
        json[used - 1] = '}';
        json[used] = '\0';
    } else {
        appendf(json, size, "}");
    }
}

static void scheduler_process(SharedState *state, int event_fd) {
    struct sigaction action = {0};
    action.sa_handler = on_alarm;
    sigemptyset(&action.sa_mask);
    sigaction(SIGALRM, &action, NULL);

    sigset_t blocked, wait_mask;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGALRM);
    sigprocmask(SIG_BLOCK, &blocked, &wait_mask);
    sigdelset(&wait_mask, SIGALRM);

    alarm(1);
    while (keep_running) {
        sigsuspend(&wait_mask);
        if (!keep_running || !scheduler_tick) {
            continue;
        }
        scheduler_tick = 0;
        time_t now = time(NULL);
        lock_state(state);
        for (int i = 0; i < MAX_ALARMS; i++) {
            Alarm *alarm_item = &state->alarms[i];
            time_t due = alarm_item->snooze_until
                             ? alarm_item->snooze_until
                             : alarm_item->when;
            if (alarm_item->id && alarm_item->enabled && due <= now) {
                FiredAlarm event = {
                    .id = alarm_item->id,
                    .when = due,
                };
                snprintf(event.label, sizeof(event.label), "%s",
                         alarm_item->label);
                if (alarm_item->snooze_until) {
                    alarm_item->snooze_until = 0;
                } else if (alarm_item->repeat_days) {
                    alarm_item->when =
                        next_weekly_occurrence(alarm_item->when, now,
                                               alarm_item->repeat_days,
                                               alarm_item->timezone);
                } else {
                    alarm_item->enabled = 0;
                }
                ssize_t written = write(event_fd, &event, sizeof(event));
                if (written != (ssize_t)sizeof(event)) {
                    perror("Could not notify parent about alarm");
                }
            }
        }
        unlock_state(state);
        alarm(1);
    }
    _exit(0);
}

static void record_fired_alarm(const FiredAlarm *event) {
    char path[600];
    make_path(path, sizeof(path), "history.tsv");
    FILE *file = fopen(path, "a");
    if (!file) {
        perror("Could not write alarm history");
        return;
    }
    fprintf(file, "%lld\t%d\t%s\n", (long long)event->when, event->id,
            event->label);
    fclose(file);
    printf("\nAlarm fired: %s\n", event->label);
    fflush(stdout);
}

static int delete_history_entry(int id, long long timestamp) {
    char source_path[600];
    char temp_path[604];
    make_path(source_path, sizeof(source_path), "history.tsv");
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", source_path);

    FILE *source = fopen(source_path, "r");
    if (!source) {
        if (errno == ENOENT) {
            return 0;
        }
        perror("Could not open alarm history");
        return -1;
    }
    FILE *temp = fopen(temp_path, "w");
    if (!temp) {
        perror("Could not create temporary alarm history");
        fclose(source);
        return -1;
    }

    bool removed = false;
    bool failed = false;
    char line[256];
    while (fgets(line, sizeof(line), source)) {
        long long row_timestamp = 0;
        int row_id = 0;
        if (sscanf(line, "%lld\t%d", &row_timestamp, &row_id) == 2 &&
            row_id == id && row_timestamp == timestamp) {
            removed = true;
            continue;
        }
        if (fputs(line, temp) == EOF) {
            perror("Could not write temporary alarm history");
            failed = true;
            break;
        }
    }
    if (ferror(source)) {
        perror("Could not read alarm history");
        failed = true;
    }
    if (fclose(source) == EOF) {
        perror("Could not close alarm history");
        failed = true;
    }
    if (fclose(temp) == EOF) {
        perror("Could not close temporary alarm history");
        failed = true;
    }
    if (!failed && removed && rename(temp_path, source_path) == -1) {
        perror("Could not replace alarm history");
        failed = true;
    }
    if (failed || !removed) {
        if (unlink(temp_path) == -1 && errno != ENOENT) {
            perror("Could not remove temporary alarm history");
        }
    }
    return failed ? -1 : (removed ? 1 : 0);
}

static bool alarm_fired_recently(int id, time_t now) {
    char path[600];
    make_path(path, sizeof(path), "history.tsv");
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }

    char line[256];
    bool found = false;
    while (fgets(line, sizeof(line), file)) {
        long long timestamp = 0;
        int history_id = 0;
        if (sscanf(line, "%lld\t%d", &timestamp, &history_id) == 2 &&
            history_id == id && timestamp <= (long long)now + 2 &&
            timestamp >= (long long)now - 600) {
            found = true;
        }
    }
    fclose(file);
    return found;
}

static int read_request(int client, char *buffer, size_t capacity) {
    size_t used = 0;
    size_t header_end = 0;
    size_t content_length = 0;
    while (used + 1 < capacity) {
        ssize_t amount = recv(client, buffer + used, capacity - used - 1, 0);
        if (amount <= 0) {
            return -1;
        }
        used += (size_t)amount;
        buffer[used] = '\0';
        char *end = strstr(buffer, "\r\n\r\n");
        if (end && !header_end) {
            header_end = (size_t)(end - buffer) + 4;
            char *length_header = strcasestr(buffer, "Content-Length:");
            if (length_header && length_header < end) {
                content_length = (size_t)strtoul(length_header + 15, NULL, 10);
            }
        }
        if (header_end && used >= header_end + content_length) {
            return (int)used;
        }
    }
    return -1;
}

static void respond(int client, int status, const char *reason,
                    const char *type, const char *body) {
    char header[512];
    size_t length = strlen(body);
    int count = snprintf(header, sizeof(header),
                         "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                         "Content-Length: %zu\r\nConnection: close\r\n"
                         "Cache-Control: no-store\r\n\r\n",
                         status, reason, type, length);
    if (count > 0) {
        size_t sent = 0;
        while (sent < (size_t)count) {
            ssize_t amount =
                send(client, header + sent, (size_t)count - sent, MSG_NOSIGNAL);
            if (amount < 0 && errno == EINTR) {
                continue;
            }
            if (amount <= 0) {
                return;
            }
            sent += (size_t)amount;
        }
        sent = 0;
        while (sent < length) {
            ssize_t amount =
                send(client, body + sent, length - sent, MSG_NOSIGNAL);
            if (amount < 0 && errno == EINTR) {
                continue;
            }
            if (amount <= 0) {
                return;
            }
            sent += (size_t)amount;
        }
    }
}

static char *json_string_value(char *body, const char *key, char *out,
                               size_t capacity) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    char *position = strstr(body, needle);
    if (!position || !(position = strchr(position + strlen(needle), ':'))) {
        return NULL;
    }
    position++;
    while (*position == ' ' || *position == '\t') {
        position++;
    }
    if (*position++ != '"') {
        return NULL;
    }
    size_t used = 0;
    while (*position && *position != '"' && used + 1 < capacity) {
        if (*position == '\\' && position[1]) {
            position++;
        }
        if (*position == '\n' || *position == '\r' || *position == '\t') {
            out[used++] = ' ';
        } else {
            out[used++] = *position;
        }
        position++;
    }
    out[used] = '\0';
    return out;
}

static bool json_integer_value(char *body, const char *key, long long *value) {
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    char *position = strstr(body, needle);
    if (!position || !(position = strchr(position + strlen(needle), ':'))) {
        return false;
    }
    position++;
    while (*position == ' ' || *position == '\t') {
        position++;
    }
    char *end = NULL;
    long long parsed = strtoll(position, &end, 10);
    if (end == position) {
        return false;
    }
    *value = parsed;
    return true;
}

static void handle_request(int client, SharedState *state, char *request) {
    char method[8] = {0};
    char path[256] = {0};
    if (sscanf(request, "%7s %255s", method, path) != 2) {
        respond(client, 400, "Bad Request", "application/json",
                "{\"error\":\"Invalid request\"}");
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/state") == 0) {
        char json[RESPONSE_SIZE] = {0};
        build_state_json(state, json, sizeof(json));
        respond(client, 200, "OK", "application/json", json);
        return;
    }

    if (strcmp(method, "DELETE") == 0 &&
        strncmp(path, "/api/history/", 13) == 0) {
        int id = 0;
        long long timestamp = 0;
        char trailing = '\0';
        if (sscanf(path, "/api/history/%d/%lld%c", &id, &timestamp,
                   &trailing) != 2 || id <= 0 || timestamp <= 0) {
            respond(client, 400, "Bad Request", "application/json",
                    "{\"error\":\"Invalid history entry\"}");
            return;
        }
        int result = delete_history_entry(id, timestamp);
        if (result > 0) {
            respond(client, 200, "OK", "application/json", "{\"ok\":true}");
        } else if (result == 0) {
            respond(client, 404, "Not Found", "application/json",
                    "{\"error\":\"History entry not found\"}");
        } else {
            respond(client, 500, "Internal Server Error", "application/json",
                    "{\"error\":\"Could not delete history entry\"}");
        }
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/alarms") == 0) {
        char *body = strstr(request, "\r\n\r\n");
        if (!body) {
            respond(client, 400, "Bad Request", "application/json",
                    "{\"error\":\"Missing request body\"}");
            return;
        }
        body += 4;
        char label[LABEL_SIZE] = {0};
        char *label_value = json_string_value(body, "label", label,
                                              sizeof(label));
        long long timestamp = 0;
        long long repeat_days = 0;
        char timezone[TIMEZONE_SIZE] = "UTC";
        json_integer_value(body, "days", &repeat_days);
        json_string_value(body, "timezone", timezone, sizeof(timezone));
        if (!label_value || !json_integer_value(body, "when", &timestamp)) {
            respond(client, 400, "Bad Request", "application/json",
                    "{\"error\":\"Provide a label and Unix timestamp\"}");
            return;
        }
        if (timestamp <= (long long)time(NULL) || repeat_days < 0 ||
            repeat_days > 127 || !valid_timezone_name(timezone)) {
            respond(client, 400, "Bad Request", "application/json",
                    "{\"error\":\"Choose a future alarm time, valid weekdays, and a valid timezone\"}");
            return;
        }
        for (size_t i = 0; label[i]; i++) {
            if (label[i] == '\t' || label[i] == '\n' || label[i] == '\r') {
                label[i] = ' ';
            }
        }
        int id = 0;
        lock_state(state);
        int slot = -1;
        for (int i = 0; i < MAX_ALARMS; i++) {
            if (!state->alarms[i].id) {
                slot = i;
                break;
            }
        }
        if (slot >= 0) {
            Alarm *alarm_item = &state->alarms[slot];
            alarm_item->id = state->next_id++;
            alarm_item->enabled = 1;
            alarm_item->when = (time_t)timestamp;
            alarm_item->repeat_days = (unsigned int)repeat_days;
            snprintf(alarm_item->timezone, sizeof(alarm_item->timezone), "%s",
                     timezone);
            snprintf(alarm_item->label, sizeof(alarm_item->label), "%s",
                     label[0] ? label : "Alarm");
            id = alarm_item->id;
        }
        unlock_state(state);
        if (slot < 0) {
            respond(client, 409, "Conflict", "application/json",
                    "{\"error\":\"Alarm limit reached\"}");
            return;
        }
        save_alarms(state);
        char json[256] = {0};
        appendf(json, sizeof(json),
                "{\"id\":%d,\"when\":%lld,\"enabled\":true,"
                "\"repeatDays\":%lld,\"label\":",
                id, timestamp, repeat_days);
        append_json_string(json, sizeof(json), label[0] ? label : "Alarm");
        appendf(json, sizeof(json), "}");
        respond(client, 201, "Created", "application/json", json);
        return;
    }

    if (strcmp(method, "DELETE") == 0 &&
        strncmp(path, "/api/alarms/", 12) == 0) {
        int id = atoi(path + 12);
        bool removed = false;
        lock_state(state);
        for (int i = 0; i < MAX_ALARMS; i++) {
            if (state->alarms[i].id == id) {
                memset(&state->alarms[i], 0, sizeof(state->alarms[i]));
                removed = true;
                break;
            }
        }
        unlock_state(state);
        if (removed) {
            save_alarms(state);
            respond(client, 200, "OK", "application/json", "{\"ok\":true}");
        } else {
            respond(client, 404, "Not Found", "application/json",
                    "{\"error\":\"Alarm not found\"}");
        }
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strncmp(path, "/api/alarms/", 12) == 0 &&
        strcmp(path + 12 + strspn(path + 12, "0123456789"), "/snooze") == 0) {
        int id = atoi(path + 12);
        bool found = false;
        bool can_snooze = false;
        unsigned int repeat_days = 0;
        time_t now = time(NULL);
        time_t snooze_until = now + 5 * 60;
        lock_state(state);
        for (int i = 0; i < MAX_ALARMS; i++) {
            Alarm *alarm_item = &state->alarms[i];
            if (alarm_item->id == id) {
                found = true;
                repeat_days = alarm_item->repeat_days;
                bool one_time_fired = !alarm_item->repeat_days &&
                                      !alarm_item->enabled &&
                                      alarm_item->when <= now;
                bool recurring_fired = alarm_item->repeat_days &&
                                       alarm_item->enabled &&
                                       !alarm_item->snooze_until &&
                                       alarm_fired_recently(id, now);
                if (one_time_fired || recurring_fired) {
                    if (one_time_fired) {
                        alarm_item->when = snooze_until;
                    } else {
                        alarm_item->snooze_until = snooze_until;
                    }
                    alarm_item->enabled = 1;
                    can_snooze = true;
                }
                break;
            }
        }
        unlock_state(state);
        if (!found) {
            respond(client, 404, "Not Found", "application/json",
                    "{\"error\":\"Alarm not found\"}");
        } else if (!can_snooze) {
            respond(client, 409, "Conflict", "application/json",
                    "{\"error\":\"Only a fired alarm can be snoozed\"}");
        } else {
            save_alarms(state);
            char json[128];
            snprintf(json, sizeof(json),
                     "{\"enabled\":true,\"when\":%lld,\"repeatDays\":%u}",
                     (long long)snooze_until, repeat_days);
            respond(client, 200, "OK", "application/json", json);
        }
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strncmp(path, "/api/alarms/", 12) == 0 &&
        strcmp(path + 12 + strspn(path + 12, "0123456789"), "/toggle") == 0) {
        int id = atoi(path + 12);
        bool found = false;
        bool enabled = false;
        bool expired = false;
        lock_state(state);
        for (int i = 0; i < MAX_ALARMS; i++) {
            Alarm *alarm_item = &state->alarms[i];
            if (alarm_item->id == id) {
                found = true;
                expired = !alarm_item->enabled &&
                          alarm_item->when <= time(NULL);
                if (!expired) {
                    alarm_item->enabled = !alarm_item->enabled;
                    enabled = alarm_item->enabled;
                }
                break;
            }
        }
        unlock_state(state);
        if (!found) {
            respond(client, 404, "Not Found", "application/json",
                    "{\"error\":\"Alarm not found\"}");
        } else if (expired) {
            respond(client, 409, "Conflict", "application/json",
                    "{\"error\":\"A fired alarm cannot be re-enabled\"}");
        } else {
            save_alarms(state);
            respond(client, 200, "OK", "application/json",
                    enabled ? "{\"enabled\":true}" : "{\"enabled\":false}");
        }
        return;
    }

    const char *relative = NULL;
    const char *type = "text/plain; charset=utf-8";
    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        relative = "index.html";
        type = "text/html; charset=utf-8";
    } else if (strcmp(method, "GET") == 0 &&
               strcmp(path, "/style.css") == 0) {
        relative = "style.css";
        type = "text/css; charset=utf-8";
    } else if (strcmp(method, "GET") == 0 &&
               strcmp(path, "/app.js") == 0) {
        relative = "app.js";
        type = "text/javascript; charset=utf-8";
    }
    if (relative) {
        char file_path[600];
        snprintf(file_path, sizeof(file_path), "public/%s", relative);
        FILE *file = fopen(file_path, "r");
        if (!file) {
            respond(client, 500, "Internal Server Error",
                    "text/plain; charset=utf-8",
                    "Cannot open frontend. Run the server from the project root.");
            return;
        }
        char content[RESPONSE_SIZE];
        size_t count = fread(content, 1, sizeof(content) - 1, file);
        content[count] = '\0';
        fclose(file);
        respond(client, 200, "OK", type, content);
        return;
    }

    respond(client, 404, "Not Found", "application/json",
            "{\"error\":\"Route not found\"}");
}

static int create_server(const char *port) {
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    struct addrinfo *result = NULL;
    int error = getaddrinfo("127.0.0.1", port, &hints, &result);
    if (error != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return -1;
    }
    int server = -1;
    for (struct addrinfo *item = result; item; item = item->ai_next) {
        server = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (server == -1) {
            continue;
        }
        int reuse = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (bind(server, item->ai_addr, item->ai_addrlen) == 0 &&
            listen(server, 16) == 0) {
            break;
        }
        close(server);
        server = -1;
    }
    freeaddrinfo(result);
    return server;
}

int main(int argc, char **argv) {
    const char *port = argc > 1 ? argv[1] : "8080";
    const char *configured_data = getenv("ALARM_DATA_DIR");
    if (configured_data && configured_data[0]) {
        snprintf(data_dir, sizeof(data_dir), "%s", configured_data);
    }
    if (mkdir(data_dir, 0755) == -1 && errno != EEXIST) {
        perror("Could not create data directory");
        return 1;
    }

    SharedState *state = mmap(NULL, sizeof(*state), PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (state == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    memset(state, 0, sizeof(*state));
    state->next_id = 1;
    if (sem_init(&state->mutex, 1, 1) == -1) {
        perror("sem_init");
        munmap(state, sizeof(*state));
        return 1;
    }
    load_alarms(state);

    int events[2];
    if (pipe(events) == -1) {
        perror("pipe");
        return 1;
    }
    pid_t scheduler = fork();
    if (scheduler == -1) {
        perror("fork");
        return 1;
    }
    if (scheduler == 0) {
        close(events[0]);
        scheduler_process(state, events[1]);
    }
    close(events[1]);
    int event_flags = fcntl(events[0], F_GETFL, 0);
    if (event_flags == -1 ||
        fcntl(events[0], F_SETFL, event_flags | O_NONBLOCK) == -1) {
        perror("Could not configure scheduler event pipe");
        kill(scheduler, SIGTERM);
        waitpid(scheduler, NULL, 0);
        close(events[0]);
        return 1;
    }

    struct sigaction stop_action = {0};
    stop_action.sa_handler = on_stop;
    sigemptyset(&stop_action.sa_mask);
    sigaction(SIGINT, &stop_action, NULL);
    sigaction(SIGTERM, &stop_action, NULL);

    int server = create_server(port);
    if (server == -1) {
        fprintf(stderr, "Could not listen on port %s\n", port);
        kill(scheduler, SIGTERM);
        waitpid(scheduler, NULL, 0);
        return 1;
    }
    printf("Alarm Clock running at http://localhost:%s\n", port);
    printf("Press Ctrl+C to stop. Alarm scheduler PID: %ld\n", (long)scheduler);
    fflush(stdout);

    struct pollfd descriptors[2] = {
        {.fd = server, .events = POLLIN},
        {.fd = events[0], .events = POLLIN},
    };
    while (keep_running) {
        int ready = poll(descriptors, 2, 1000);
        if (ready == -1 && errno == EINTR) {
            continue;
        }
        if (ready == -1) {
            perror("poll");
            break;
        }
        if (descriptors[1].revents & POLLIN) {
            FiredAlarm event;
            while (read(events[0], &event, sizeof(event)) ==
                   (ssize_t)sizeof(event)) {
                record_fired_alarm(&event);
                save_alarms(state);
            }
        }
        if (descriptors[0].revents & POLLIN) {
            int client = accept(server, NULL, NULL);
            if (client == -1) {
                if (errno != EINTR) {
                    perror("accept");
                }
                continue;
            }
            char request[REQUEST_SIZE];
            int request_length = read_request(client, request, sizeof(request));
            if (request_length > 0) {
                handle_request(client, state, request);
            } else {
                respond(client, 400, "Bad Request", "application/json",
                        "{\"error\":\"Incomplete request\"}");
            }
            close(client);
        }
    }

    close(server);
    close(events[0]);
    kill(scheduler, SIGTERM);
    waitpid(scheduler, NULL, 0);
    sem_destroy(&state->mutex);
    munmap(state, sizeof(*state));
    return 0;
}
