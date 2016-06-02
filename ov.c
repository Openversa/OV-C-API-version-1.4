/*

    COPYRIGHT AND PERMISSION NOTICE
    Copyright (c) 2015-2016 miVersa Inc
    All rights reserved.
    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
    OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
    DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
    OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
    USE OR OTHER DEALINGS IN THE SOFTWARE.

    Except as contained in this notice, the name of a copyright holder shall
    not be used in advertising or otherwise to promote the sale, use or other
    dealings in this Software without prior written authorization of the
    copyright holder.

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "ov_api.h"
#include "jsmn.h"
#include "ov_jsmn.h"

/* define OV_INSTALL to install credentials at runtime */ 
#define OV_INSTALL 1

#ifdef OV_INSTALL
static char *OPENVERSA_APP_ID;
static char *OPENVERSA_APP_SECRET;
static char *OPENVERSA_URL;
static char *OPENVERSA_USER_NAME;
#else
/* define your credentials here! */
#error "Credentials must be defined"
#define OPENVERSA_APP_ID "-- insert APP ID here --"
#define OPENVERSA_APP_SECRET "-- insert SECRET here --"
#define OPENVERSA_URL "-- insert URL here --"
#define OPENVERSA_USER_NAME "-- insert USER_NAME here --"
#endif

/*************** Micro getopt() *********************************************/
#define	OPTION(c,v)	(_O&2&&**v?*(*v)++:!c||_O&4?0:(!(_O&1)&& \
				(--c,++v),_O=4,c&&**v=='-'&&v[0][1]?*++*v=='-'\
				&&!v[0][1]?(--c,++v,0):(_O=2,*(*v)++):0))
#define	OPTARG(c,v)	(_O&2?**v||(++v,--c)?(_O=1,--c,*v++): \
				(_O=4,(char*)0):(char*)0)
#define	OPTONLYARG(c,v)	(_O&2&&**v?(_O=1,--c,*v++):(char*)0)
#define	ARG(c,v)	(c?(--c,*v++):(char*)0)

static int _O = 0;		/* Internal state */
/*************** Micro getopt() *********************************************/

extern int ov_main_test(ov_handle_t*, int argc, char *argv[]);
extern int ov_main_monitor(ov_handle_t*, int argc, char *argv[]);

typedef struct {
    const char *name;
    int (*main_proc)(ov_handle_t*, int, char*[]);
    int arg_min;
    const char *arg_description;
} ov_dispatcher_t;

typedef struct {
    char app_id[128];
    char app_secret[128];
    char username[128];
    char url[128];
} ov_config_t;

static int ov_main_upload(ov_handle_t *ov_handle, int argc, char *argv[])
{
    int group_id = ov_group_name_to_id(ov_handle, argv[1]);

    if (group_id) {
        ov_put_file_param_t pfp = {
            .notify_member = 1,
            .name = argv[2],
            .title = argv[2]
        };
        ov_msg_handle_t *msg_handle = ov_put_file(ov_handle, group_id, &pfp);
        if (msg_handle) {
            ov_msg_cleanup(msg_handle);
        } else {
            printf("Error: File upload error\n");
        }
        return msg_handle ? 0 : 1;
    }
    printf("Error: Group %s not found\n", argv[1]);
    return 1;
}

/* file download */
static int ov_main_download(ov_handle_t *ov_handle, int argc, char *argv[])
{
    int group_id = ov_group_name_to_id(ov_handle, argv[1]);

    if (group_id) {
        ov_msg_handle_t *msg_handle;
        ov_get_file_param_t gfp = {
            .name = argv[2],
            .local_path = argv[3]
        };
        if ((msg_handle = ov_get_file(ov_handle, group_id, &gfp))) {
            ov_msg_cleanup(msg_handle);
        } else {
            printf("Error: File download error\n");
        }
        return msg_handle ? 0 : 1;
    }
    printf("Error: Group %s not found\n", argv[1]);
    return 1;
}

/* print out group list */
static int ov_main_group(ov_handle_t *ov_handle, int argc, char *argv[])
{
    ov_group_list_param_t gp = {
        .entry_count = -1,
        .do_badges = 0
    };
    ov_msg_handle_t *msg_handle = ov_get_group_list(ov_handle, &gp);

    if (msg_handle) {
        int i;
        ov_group_list_status_t *ge;
        for (i = 0, ge = gp.entries; i < gp.entry_count; i++, ge++) {
            printf("%s\n", ge->name);
        }
        ov_msg_cleanup(msg_handle);
    } else {
        printf("Error: Get OpenVersa group list failed\n");
    }
    return msg_handle ? 0 : 1;
}

static int ov_main_files(ov_handle_t *ov_handle, int argc, char *argv[])
{
    int group_id = ov_group_name_to_id(ov_handle, argv[1]);
    if (group_id) {
        ov_file_list_param_t flp = {};
        ov_msg_handle_t *msg_handle = ov_get_file_list(ov_handle, group_id, &flp);
        if (msg_handle) {
            int i;
            ov_file_list_status_t *fs;
            for (i = 0, fs = flp.entries; i < flp.entry_count; i++, fs++) {
                printf("%s\n", fs->name);
            }
            ov_msg_cleanup(msg_handle);
        } else {
            printf("OpenVersa file list get failed for group %s\n", argv[1]);
        }
        return msg_handle ? 0 : 1;
    }
    printf("Error: Group %s not found\n", argv[1]);
    return 1;
}

#ifdef OV_INSTALL
static int ov_main_install(ov_handle_t *ov_handle, int argc, char *argv[])
{
    jsmn_parser parser;
    jsmnerr_t ret;
    jsmntok_t tokens[100] = {};
    char *data, **ptr, *dst, *keyword;
    ov_config_t cfg = {};
    size_t l;
    FILE *f = fopen(argv[1], "rb");
    int i;

    if (!f) {
        printf("credential file %s not found!\n", argv[1]);
        return -1;
    }

    /* put file contents into memory */
    fseek(f, 0, SEEK_END);
    if ((data = malloc((l = ftell(f)) + 1)) == NULL) {
        fclose(f);
        return -1;
    }
    rewind(f);
    fread(data, l, 1, f);
    data[l] = 0;

    /* parse the data */
    jsmn_init(&parser);
    if ((ret = ov_jsmn_parse(&parser, data, tokens, 100))) {
        printf("JSON Parsing error %d\n", ret);
        return -1;
    }
    for (i = 0; i < 4; i++) {
        switch(i) {
        case 0: ptr = &OPENVERSA_APP_ID;    dst = cfg.app_id;        keyword = "appid"; break;
        case 1: ptr = &OPENVERSA_APP_SECRET; dst = cfg.app_secret;   keyword = "secret"; break;
        case 2: ptr = &OPENVERSA_URL;       dst = cfg.url;          keyword = "url"; break;
        case 3: ptr = &OPENVERSA_USER_NAME; dst = cfg.username;    keyword = "username"; break;
        }
        if ((*ptr = jsmn_get_string(tokens, data, keyword))) {
            strcpy(dst, *ptr);
        }
        else {
            printf("JSON Parsing error entry %s not found\n", keyword);
            return -1;
        }
    }
    {
        /* load credentials */
        FILE *f = fopen(".ov", "wb");
        if (!f) {
            printf("could not open \".ov\" for writing!\n");
            return -1;
        }
        else {
            printf("credentials updated!\n");
            fwrite(&cfg, sizeof(cfg), 1, f);
            fclose(f);
        }
    }

    return ret? -1 : 0;
}
#endif // OV_INSTALL

static int ov_main_config(ov_handle_t *ov_handle, int argc, char *argv[])
{
    printf("APP_ID     = %s\n", OPENVERSA_APP_ID);
    printf("APP_SECRET = %s\n", OPENVERSA_APP_SECRET);
    printf("URL        = %s\n", OPENVERSA_URL);
    printf("USER_NAME  = %s\n", OPENVERSA_USER_NAME);
    return 0;
}

static ov_dispatcher_t main_dispatcher[] = {
    { "upload",     ov_main_upload,     3, "<group name> <local path> <ov filename>" },
    { "download",   ov_main_download,   3, "<group name> <ov filename> <local path>" },
    { "monitor",    ov_main_monitor,    1, "<group name>" },
    { "test",       ov_main_test,       0, "" },
    { "group",      ov_main_group,      0, "" },
    { "file",       ov_main_files,      1, "<group name>" },
#ifdef OV_INSTALL
    { "install",    NULL /*ov_main_install */,    1, "<credentials file name>" },
#endif
    { "config",     ov_main_config,     0, "" },
    { NULL },
};

static int ov_usage(void)
{
    ov_dispatcher_t *md;
    printf("\nUsage:\n\n");
    for (md = main_dispatcher; md->name; md++) {
        printf("ov [-v] %s %s\n", md->name, md->arg_description);
    }
    printf("\nOptions:\n\n -v Verbose mode\n\n");
    return 1;
}


int main(int argc, char *argv[])
{
    ov_dispatcher_t *md;
    int option, verbose = 0;

    if (argc < 2) {
        return ov_usage();
    }

    while ((option = OPTION(argc, argv)) != 0) {
        switch (option) {
        case 'v':
            printf("Verbose mode specified\n");
            verbose = 1;
            break;
        default:
            return ov_usage();
        }
    }
#ifdef OV_INSTALL
    /* read the config */
    if (strcmp(argv[0], "install") == 0) {
        if (ov_main_install(NULL, argc, argv)) {
            /* install error */
            return -1;
        }
    }
    {
        /* load credentials */
        FILE *f = fopen(".ov", "rb");
        if (!f) {
            printf("install the credentials!\n");
            return -1;
        }
        else {
            ov_config_t cfg;
            fread(&cfg, sizeof(cfg), 1, f);
            fclose(f);
            OPENVERSA_APP_ID = strdup(cfg.app_id);
            OPENVERSA_APP_SECRET = strdup(cfg.app_secret);
            OPENVERSA_URL = strdup(cfg.url);
            OPENVERSA_USER_NAME = strdup(cfg.username);
        }
    }
#endif // OV_INSTALL
    for (md = main_dispatcher; md->name; md++) {
        if (!strcmp(argv[0], md->name)) {
            int ret;
            ov_handle_t *ov_handle;
            ov_param_t ovp = {
                .debug = verbose,
                .get_stats = 1
            };
            if ((argc - 1) < md->arg_min) {
                return ov_usage();
            }
            if (!md->main_proc) {
                return 0;
            }
            if (!((ov_handle = ov_init(OPENVERSA_APP_ID, OPENVERSA_APP_SECRET,
                                              OPENVERSA_USER_NAME, OPENVERSA_URL, &ovp)))) {
                printf("OpenVersa initialization failed\n");
                return 1;
            }
            ret = md->main_proc(ov_handle, argc, argv);
            ov_shutdown(ov_handle);
            return ret;
        }
    }
    return ov_usage();
}
