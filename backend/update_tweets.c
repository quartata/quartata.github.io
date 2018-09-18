#include <cJSON/cJSON.h>
#include <mongoose/mongoose.h>
#include <postgresql/libpq-fe.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LOGFILE "log"

#define AUTH_REQUEST "https://api.twitter.com/oauth2/token"
#define AUTH_HEADER "Content-Type: application/x-www-form-urlencoded\r\nAuthorization: Basic "
#define BEARER_AUTH_HEADER "Authorization: Bearer "

#define RATELIMIT_REQUEST "https://api.twitter.com/1.1/application/rate_limit_status.json?resources=statuses"

#define TIMELINE_RESOURCE "/statuses/user_timeline"
#define TIMELINE_REQUEST "https://api.twitter.com/1.1" TIMELINE_RESOURCE ".json?user_id=712397333120421889&count=200&since_id=%s"
#define TIMELINE_REQUEST_NO_SINCE "https://api.twitter.com/1.1" TIMELINE_RESOURCE ".json?user_id=712397333120421889&count=200"
#define MAX_ID "&max_id=%s"

#define EXEC(x, y) PQclear(PQexec(x, y))
#define EXEC_PARAMS(a, b, c, d, e, f, g, h) PQclear(PQexecParams(a, b, c, d, e, f, g, h))

#define WITH_JSON(x) \
    if (event != MG_EV_HTTP_REPLY) \
    { \
        return; \
    } \
    conn->flags |= MG_F_CLOSE_IMMEDIATELY; \
    struct http_message *msg = (struct http_message *) reqPtr; \
    cJSON *x = cJSON_Parse(msg->body.p);


static struct mg_mgr mgr;
static PGconn *dbConn;

static char *bearerAuthHeader;
static char done, latestImageSet, maxTIDSet;

static char *requestBuffer;
static size_t bufSize;

static int16_t remaining = -1;
static uint64_t reset;
static char *maxID;


void begin_tweet_request(void);


void fetch_ratelimit_status(struct mg_connection *conn, int event, void *reqPtr)
{
    WITH_JSON(rateLimitJSON);

    cJSON *resource = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(rateLimitJSON, "resources"), "statuses");
    cJSON *ratelimits = cJSON_GetObjectItemCaseSensitive(resource, TIMELINE_RESOURCE);

    remaining = (int16_t) cJSON_GetObjectItemCaseSensitive(ratelimits, "remaining")->valueint;
    reset = (uint64_t) cJSON_GetObjectItemCaseSensitive(ratelimits, "reset")->valueint;

    cJSON_Delete(rateLimitJSON);
    begin_tweet_request();
}


void begin_ratelimit_request(void)
{
    mg_connect_http(&mgr, fetch_ratelimit_status, RATELIMIT_REQUEST, bearerAuthHeader, NULL);
}


void do_auth(struct mg_connection *conn, int event, void *reqPtr)
{
    WITH_JSON(bearerJSON);

    const char *bearerToken = cJSON_GetObjectItemCaseSensitive(bearerJSON, "access_token")->valuestring;

    bearerAuthHeader = (char *) malloc(sizeof(BEARER_AUTH_HEADER) + strlen(bearerToken) + 2);
    sprintf(bearerAuthHeader, "%s%s\r\n", BEARER_AUTH_HEADER, bearerToken);

    cJSON_Delete(bearerJSON);
    begin_ratelimit_request();
}


void fetch_tweets(struct mg_connection *conn, int event, void *reqPtr)
{
    WITH_JSON(tweets);

    static const int formats[] = {0};

    if (!tweets || !tweets->child)
    {
        done = 1;
        cJSON_Delete(tweets);

        return;
    }

    char *tid;
    cJSON *tweet, *start;

    if (!maxTIDSet)
    {
        tid = cJSON_GetObjectItemCaseSensitive(tweets->child, "id_str")->valuestring;
        const int lengths[] = {strlen(tid)};

        EXEC(dbConn, "BEGIN; TRUNCATE maxTID;");
        EXEC_PARAMS(dbConn, "INSERT INTO maxTID (maxTID) VALUES ($1);", 1, NULL, (const char * const *) &tid, lengths, formats, 1);
        EXEC(dbConn, "COMMIT;");

        maxTIDSet = 1;
        start = tweets;
    }
    else
    {
        if (!tweets->next)
        {
            done = 1;
            cJSON_Delete(tweets);

            return;
        }

        start = tweets->next;
    }

    cJSON_ArrayForEach(tweet, start)
    {
        if (cJSON_GetObjectItemCaseSensitive(tweet, "retweeted_status"))
        {
            continue;
        }

        tid = cJSON_GetObjectItemCaseSensitive(tweet, "id_str")->valuestring;
        const int lengths[] = {strlen(tid)};

        EXEC_PARAMS(dbConn, "INSERT INTO tweets (tid) VALUES ($1);", 1, NULL, (const char * const *) &tid, lengths, formats, 1);

        if (!latestImageSet)
        {
            cJSON *media = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(tweet, "extended_entities"), "media");

            if (media)
            {
                cJSON *pic;
                cJSON_ArrayForEach(pic, media)
                {
                    if (strcmp(cJSON_GetObjectItemCaseSensitive(pic, "type")->valuestring, "photo"))
                    {
                        continue;
                    }
                    else
                    {
                        char *url = cJSON_GetObjectItemCaseSensitive(pic, "media_url_https")->valuestring;
                        const int lengths[] = {strlen(url)};

                        EXEC(dbConn, "BEGIN; TRUNCATE latestImage;");
                        EXEC_PARAMS(dbConn, "INSERT INTO latestImage (link) VALUES ($1);", 1, NULL, (const char * const *) &url, lengths, formats, 1);
                        EXEC(dbConn, "COMMIT;");

                        latestImageSet = 1;
                        break;
                    }
                }
            }
        }
    }

    sprintf(requestBuffer + bufSize - (sizeof(MAX_ID) + 21), MAX_ID, tid);
    cJSON_Delete(tweets);

    if (time(NULL) > reset)
    {
        remaining = -1;
        begin_ratelimit_request();
    }
    else
    {
        remaining--;
        begin_tweet_request();
    }
}


void begin_tweet_request(void)
{
    mg_connect_http(&mgr, fetch_tweets, requestBuffer, bearerAuthHeader, NULL);
}


int main(void)
{
    dbConn = PQconnectdb(getenv("DATABASE_URL"));

    PGresult *maxTIDResult = PQexec(dbConn, "SELECT maxTID from maxTID LIMIT 1;");
    const char *maxTID;

    const char *requestTemplate;

    if (PQntuples(maxTIDResult) > 0)
    {
        maxTID = PQgetvalue(maxTIDResult, 0, 0);
        bufSize = sizeof(TIMELINE_REQUEST) - 2 + PQgetlength(maxTIDResult, 0, 0) + sizeof(MAX_ID) + 20;
        requestTemplate = TIMELINE_REQUEST;
    }
    else
    {
        bufSize = sizeof(TIMELINE_REQUEST_NO_SINCE) + sizeof(MAX_ID) + 20;
        requestTemplate = TIMELINE_REQUEST_NO_SINCE;
    }

    char buf[bufSize];
    requestBuffer = buf;

    sprintf(buf, requestTemplate, maxTID);
    PQclear(maxTIDResult);

    mg_mgr_init(&mgr, NULL);

#ifdef LOGGING
    mgr.hexdump_file = LOGFILE;
#endif

    const char *bearerTokenCreds = getenv("BEARER_TOKEN_CREDS");
    char authHeader[sizeof(AUTH_HEADER) + strlen(bearerTokenCreds) + 2];
    sprintf(authHeader, AUTH_HEADER "%s\r\n", bearerTokenCreds);

    mg_connect_http(&mgr, do_auth, AUTH_REQUEST, authHeader, "grant_type=client_credentials");

    while (!done)
    {
        mg_mgr_poll(&mgr, 1000);

        if (remaining == 0)
        {
            sleep(reset - time(NULL));
            begin_ratelimit_request();
        }
    }

    EXEC(dbConn, "NOTIFY newTweets;");

    mg_mgr_free(&mgr);
    PQfinish(dbConn);
    free(bearerAuthHeader);

    return 0;
}