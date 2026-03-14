#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "config.h"
#include "common.h"

int g_log_level = LOG_LVL_INFO;

static char *trim(char *s)
{
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

int config_load(const char *path, struct gateway_config *cfg)
{
    FILE *f;
    char line[256];
    char section[64];
    struct path_config *cur_path;

    f = fopen(path, "r");
    if (!f) {
        LOG_ERR("cannot open config: %s", path);
        return -1;
    }

    /* Defaults */
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->mode, "tx", sizeof(cfg->mode) - 1);
    cfg->k = 4;
    cfg->redundancy_ratio = 1.5f;
    cfg->block_timeout_ms = 5;
    cfg->max_payload = 1400;
    cfg->window_size = 8;
    strncpy(cfg->strategy_type, "fixed", sizeof(cfg->strategy_type) - 1);
    cfg->probe_interval_ms = 100;
    cfg->probe_loss_threshold = 0.3f;
    cfg->listen_port = 7000;
    cfg->metrics_port   = 0;
    cfg->log_level      = 2;

    section[0] = '\0';
    cur_path = NULL;

    while (fgets(line, (int)sizeof(line), f)) {
        char *p = trim(line);
        if (*p == '#' || *p == '\0') continue;

        if (*p == '[') {
            /* Section header */
            char *end = strchr(p, ']');
            if (!end) continue;
            *end = '\0';
            strncpy(section, trim(p + 1), sizeof(section) - 1);
            section[sizeof(section) - 1] = '\0';

            if (strncmp(section, "path.", 5) == 0) {
                if (cfg->path_count >= MAX_PATHS) {
                    LOG_WARN("too many paths, ignoring [%s]", section);
                    cur_path = NULL;
                } else {
                    cur_path = &cfg->paths[cfg->path_count++];
                    memset(cur_path, 0, sizeof(*cur_path));
                    cur_path->weight = 1.0f;
                    cur_path->enabled = true;
                    {
                        size_t nlen = strlen(section + 5);
                        if (nlen >= sizeof(cur_path->name))
                            nlen = sizeof(cur_path->name) - 1;
                        memcpy(cur_path->name, section + 5, nlen);
                        cur_path->name[nlen] = '\0';
                    }
                }
            } else {
                cur_path = NULL;
            }
            continue;
        }

        /* key = value */
        {
            char *eq = strchr(p, '=');
            char *key, *val;
            if (!eq) continue;
            *eq = '\0';
            key = trim(p);
            val = trim(eq + 1);

            if (strcmp(section, "general") == 0) {
                if      (!strcmp(key, "mode"))
                    strncpy(cfg->mode, val, sizeof(cfg->mode) - 1);
                else if (!strcmp(key, "tun_name"))
                    strncpy(cfg->tun_name, val, sizeof(cfg->tun_name) - 1);
                else if (!strcmp(key, "tun_addr"))
                    strncpy(cfg->tun_addr, val, sizeof(cfg->tun_addr) - 1);
                else if (!strcmp(key, "listen_port"))
                    cfg->listen_port = atoi(val);
                else if (!strcmp(key, "metrics_port"))
                    cfg->metrics_port = atoi(val);
                else if (!strcmp(key, "log_level"))
                    cfg->log_level = atoi(val);
                else if (!strcmp(key, "crypto_key"))
                    strncpy(cfg->crypto_key, val, sizeof(cfg->crypto_key) - 1);
            } else if (strcmp(section, "coding") == 0) {
                if      (!strcmp(key, "k"))
                    cfg->k = atoi(val);
                else if (!strcmp(key, "redundancy_ratio"))
                    cfg->redundancy_ratio = (float)atof(val);
                else if (!strcmp(key, "block_timeout_ms"))
                    cfg->block_timeout_ms = atoi(val);
                else if (!strcmp(key, "max_payload"))
                    cfg->max_payload = atoi(val);
                else if (!strcmp(key, "window_size"))
                    cfg->window_size = atoi(val);
            } else if (strcmp(section, "strategy") == 0) {
                if      (!strcmp(key, "type"))
                    strncpy(cfg->strategy_type, val,
                            sizeof(cfg->strategy_type) - 1);
                else if (!strcmp(key, "probe_interval_ms"))
                    cfg->probe_interval_ms = atoi(val);
                else if (!strcmp(key, "probe_loss_threshold"))
                    cfg->probe_loss_threshold = (float)atof(val);
            } else if (cur_path != NULL) {
                if      (!strcmp(key, "interface"))
                    strncpy(cur_path->interface, val,
                            sizeof(cur_path->interface) - 1);
                else if (!strcmp(key, "remote_ip"))
                    strncpy(cur_path->remote_ip, val,
                            sizeof(cur_path->remote_ip) - 1);
                else if (!strcmp(key, "remote_port"))
                    cur_path->remote_port = (uint16_t)atoi(val);
                else if (!strcmp(key, "weight"))
                    cur_path->weight = (float)atof(val);
                else if (!strcmp(key, "enabled"))
                    cur_path->enabled = (strcmp(val, "true") == 0);
            }
        }
    }

    /* Post-parse validation: clamp values that would cause UB downstream. */
    if (cfg->k < 1)       cfg->k = 1;
    if (cfg->k > MAX_K)   cfg->k = MAX_K;
    if (cfg->window_size < 1) cfg->window_size = 1;
    if (cfg->window_size > MAX_WINDOW) cfg->window_size = MAX_WINDOW;
    if (cfg->max_payload < 1) cfg->max_payload = 1;
    if (cfg->max_payload > MAX_PAYLOAD) cfg->max_payload = MAX_PAYLOAD;
    fclose(f);
    return 0;
}
