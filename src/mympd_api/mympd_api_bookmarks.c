/*
 SPDX-License-Identifier: GPL-2.0-or-later
 myMPD (c) 2018-2021 Juergen Mang <mail@jcgames.de>
 https://github.com/jcorporation/mympd
*/

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>

#include "../../dist/src/sds/sds.h"
#include "../sds_extras.h"
#include "../../dist/src/frozen/frozen.h"
#include "../log.h"
#include "../list.h"
#include "config_defs.h"
#include "../utility.h"
#include "mympd_api_utility.h"
#include "mympd_api_bookmarks.h"

//private definitions
static bool write_bookmarks_line(FILE *fp, int id, const char *name,
                                 const char *uri, const char *type);

//public functions
bool mympd_api_bookmark_update(t_config *config, const int id, const char *name, 
                               const char *uri, const char *type)
{
    sds tmp_file = sdscatfmt(sdsempty(), "%s/state/bookmark_list.XXXXXX", config->varlibdir);
    int fd = mkstemp(tmp_file);
    if (fd < 0 ) {
        MYMPD_LOG_ERROR("Can not open file \"%s\" for write: %s", tmp_file, strerror(errno));
        sdsfree(tmp_file);
        return false;
    }
    FILE *fo = fdopen(fd, "w");
    int line_nr = 0;
    bool inserted = false;
    sds b_file = sdscatfmt(sdsempty(), "%s/state/bookmark_list", config->varlibdir);
    FILE *fi = fopen(b_file, "r");
    if (fi != NULL) {
        char *line = NULL;
        size_t n = 0;
        ssize_t read;
        while ((read = getline(&line, &n, fi)) > 0) {
            char *lname = NULL;
            char *luri = NULL;
            char *ltype = NULL;
            int lid;
            int je = json_scanf(line, read, "{id: %d, name: %Q, uri: %Q, type: %Q}", &lid, &lname, &luri, &ltype);
            if (je == 4) {
                if (name != NULL) {
                    if (strcmp(name, lname) < 0) {
                        line_nr++;
                        bool rc = write_bookmarks_line(fo, line_nr, name, uri, type);
                        if (rc == true) {
                            inserted = true;
                        }
                    }
                }
                if (lid != id) {
                    line_nr++;
                    write_bookmarks_line(fo, line_nr, lname, luri, ltype);
                }
            }
            else {
                MYMPD_LOG_ERROR("Can not read bookmarks line");
                MYMPD_LOG_DEBUG("Errorneous line: %s", line);
            }
            FREE_PTR(lname);
            FREE_PTR(luri);
            FREE_PTR(ltype);
        }
        FREE_PTR(line);
        fclose(fi);
    }
    if (inserted == false && name != NULL) {
        line_nr++;
        write_bookmarks_line(fo, line_nr, name, uri, type);
    }
    fclose(fo);
    
    if (rename(tmp_file, b_file) == -1) {
        MYMPD_LOG_ERROR("Rename file from \"%s\" to \"%s\" failed: %s", tmp_file, b_file, strerror(errno));
        sdsfree(tmp_file);
        sdsfree(b_file);
        return false;
    }
    sdsfree(tmp_file);
    sdsfree(b_file);
    return true;
}

bool mympd_api_bookmark_clear(t_config *config) {
    sds b_file = sdscatfmt(sdsempty(), "%s/state/bookmark_list", config->varlibdir);
    int rc = unlink(b_file);
    if (rc == 0) {
        sdsfree(b_file);
        return true;
    }
    if (rc == -1 && errno != ENOENT) {
        MYMPD_LOG_ERROR("Error removing file \"%s\": %s", b_file, strerror(errno));
    }
    else {
        //ignore error
        MYMPD_LOG_DEBUG("Error removing file \"%s\": %s", b_file, strerror(errno));
    }
    sdsfree(b_file);
    return false;
}

sds mympd_api_bookmark_list(t_config *config, sds buffer, sds method, long request_id, unsigned int offset) {
    char *line = NULL;
    char *crap = NULL;
    size_t n = 0;
    unsigned entity_count = 0;
    unsigned entities_returned = 0;
    
    sds b_file = sdscatfmt(sdsempty(), "%s/state/bookmark_list", config->varlibdir);
    FILE *fi = fopen(b_file, "r");

    buffer = jsonrpc_result_start(buffer, method, request_id);
    buffer = sdscat(buffer, "\"data\":[");

    if (fi == NULL) {
        //create empty bookmarks file
        fi = fopen(b_file, "w");
        if (fi == NULL) {
            MYMPD_LOG_ERROR("Can't open %s for write", b_file);
            buffer = sdscrop(buffer);
            buffer = jsonrpc_respond_message(buffer, method, request_id, true, 
                "general", "error", "Failed to open bookmarks file");
            sdsfree(b_file);
            return buffer;
        }
        fclose(fi);
    }
    else {
        while (getline(&line, &n, fi) > 0) {
            entity_count++;
            if (entity_count > offset && entity_count <= offset + config->max_elements_per_page) {
                if (entities_returned++) {
                    buffer = sdscat(buffer, ",");
                }
                strtok_r(line, "\n", &crap);
                buffer = sdscat(buffer, line);
            }
        }
        FREE_PTR(line);
        fclose(fi);

    }
    sdsfree(b_file);
    buffer = sdscat(buffer, "],");
    buffer = tojson_long(buffer, "totalEntities", entity_count, true);
    buffer = tojson_long(buffer, "offset", offset, true);
    buffer = tojson_long(buffer, "returnedEntities", entities_returned, false);
    buffer = jsonrpc_result_end(buffer);
    return buffer;
}

//private functions
static bool write_bookmarks_line(FILE *fp, int id, const char *name, 
                                 const char *uri, const char *type)
{
    sds line = sdscat(sdsempty(), "{");
    line = tojson_long(line, "id", id, true);
    line = tojson_char(line, "name", name, true);
    line = tojson_char(line, "uri", uri, true);
    line = tojson_char(line, "type", type, false);
    line = sdscat(line, "}\n");
    int rc = fputs(line, fp);
    sdsfree(line);
    if (rc > 0) {
        return true;
    }
    MYMPD_LOG_ERROR("Can't write bookmarks line to file");
    return false;
}
