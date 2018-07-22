#include <mongoose/mongoose.h>
#include <postgresql/libpq-fe.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define PROFILE "https://twitter.com/quartatertots/status/"
#define STATUS_SIZE (sizeof(PROFILE) + 20)


static struct mg_str latestImage;
static struct mg_str *tweets;
static char (*tweetURLs)[STATUS_SIZE];

static uint64_t capacity;
static uint64_t numTweets;
static int64_t maxSerial;

static struct mg_str nil = {NULL, 0};

static atomic_flag inImageRequest = ATOMIC_FLAG_INIT;
static atomic_flag inImageUpdate = ATOMIC_FLAG_INIT;

static atomic_flag inTweetRequest = ATOMIC_FLAG_INIT;
static atomic_flag inTweetUpdate = ATOMIC_FLAG_INIT;

static PGconn *conn;
static struct mg_mgr mgr;


void handler(struct mg_connection *conn, int event, void *reqPtr)
{
    if (event == MG_EV_HTTP_REQUEST)
    {
        struct http_message *msg = (struct http_message *) reqPtr;

        switch (msg->uri.p[1])
        {
            case 'i':
                while (atomic_flag_test_and_set(&inImageUpdate)) usleep(0);
                atomic_flag_clear(&inImageUpdate);
                atomic_flag_test_and_set(&inImageRequest);

                if (!latestImage.p)
                {
                    mg_http_send_error(conn, 404, NULL);
                    return;
                }

                mg_http_send_redirect(conn, 302, latestImage, nil);

                atomic_flag_clear(&inImageRequest);
                break;
            case 'r':
                while (atomic_flag_test_and_set(&inTweetUpdate)) usleep(0);
                atomic_flag_clear(&inTweetUpdate);
                atomic_flag_test_and_set(&inTweetRequest);

                if (!tweets)
                {
                    mg_http_send_error(conn, 404, NULL);
                    return;
                }

                unsigned int i = rand() * numTweets / ((double) RAND_MAX + 1);

                mg_http_send_redirect(conn, 302, tweets[i], nil);

                atomic_flag_clear(&inTweetRequest);
                break;
            default:
                mg_http_send_error(conn, 404, NULL);
        }
    }
}


void update_tweet_list(void)
{
    PGresult *result = PQexec(conn, "SELECT link from latestImage LIMIT 1;");

    if (PQntuples(result) > 0)
    {
        int urlLength = PQgetlength(result, 0, 0);

        atomic_flag_test_and_set(&inImageUpdate);
        while (atomic_flag_test_and_set(&inImageRequest)) usleep(0);
        atomic_flag_clear(&inImageRequest);

        if (!latestImage.p)
        {
            latestImage.p = malloc(urlLength);
        }
        else if (urlLength > latestImage.len)
        {
            latestImage.p = realloc((void *) latestImage.p, urlLength);
        }

        memcpy((void *) latestImage.p, PQgetvalue(result, 0, 0), urlLength);
        latestImage.len = urlLength;

        atomic_flag_clear(&inImageUpdate);
    }

    PQclear(result);

    const char *idPointer = (const char *) &maxSerial;
    static const int lengths[] = {sizeof(uint64_t)};
    static const int formats[] = {1};

    result = PQexecParams(conn, "SELECT id, tid from tweets WHERE id > $1::BIGINT;", 1, NULL, &idPointer, lengths, formats, 1);

    int rows = PQntuples(result);

    if (rows > 0)
    {
        atomic_flag_test_and_set(&inTweetUpdate);
        while (atomic_flag_test_and_set(&inTweetRequest)) usleep(0);
        atomic_flag_clear(&inTweetRequest);

        unsigned long i = numTweets;
        numTweets += rows;

        if (numTweets > capacity)
        {
            capacity = numTweets + 32;

            if (!tweets)
            {
                tweets = malloc(sizeof(struct mg_str) * capacity);
                tweetURLs = malloc(STATUS_SIZE * capacity);
            }
            else
            {
                tweets = realloc(tweets, sizeof(struct mg_str) * capacity);
                char (*newURLBlock)[STATUS_SIZE] = realloc(tweetURLs, STATUS_SIZE * capacity);

                if (newURLBlock != tweetURLs)
                {
                    for (int j = 0; j < i; j++)
                    {
                        tweets[j].p = (const char *) &newURLBlock[j];
                    }
                }

                tweetURLs = newURLBlock;
            }
        }

        int j;
        for (j = 0; i < numTweets; i++, j++)
        {
            char buffer[STATUS_SIZE];

            tweets[i].len = sprintf(tweetURLs[i], PROFILE "%s", PQgetvalue(result, j, 1));
            tweets[i].p = (const char *) &tweetURLs[i];
        }

        atomic_flag_clear(&inTweetUpdate);
        maxSerial = *((int64_t *) PQgetvalue(result, j - 1, 0));
    }

    PQclear(result);
}


void *listen_for_update(void *_)
{
    PQclear(PQexec(conn, "LISTEN newTweets;"));

    int psqlSock = PQsocket(conn);
    fd_set mask;

    FD_ZERO(&mask);
    FD_SET(psqlSock, &mask);

    while (1)
    {
        if (select(psqlSock + 1, &mask, NULL, NULL, NULL) > 0)
        {
            PQconsumeInput(conn);
            PGnotify *notify;

            if ((notify = PQnotifies(conn)) != NULL)
            {
                PQfreemem(notify);
                update_tweet_list();
            }
        }
    }
}


void exit_handler(void)
{
    if (tweets)
    {
        free(tweets);
        free(tweetURLs);
    }

    if (latestImage.p)
    {
        free((void *) latestImage.p);
    }

    mg_mgr_free(&mgr);
    PQfinish(conn);
}


int main(void)
{
    atexit(exit_handler);
    signal(SIGTERM, exit);
    signal(SIGUSR1, exit);

    srand(time(NULL));

    conn = PQconnectdb(getenv("DATABASE_URL"));

    update_tweet_list();

    pthread_t thread;
    pthread_create(&thread, NULL, listen_for_update, NULL);

    mg_mgr_init(&mgr, NULL);

    struct mg_connection *conn;
    conn = mg_bind(&mgr, getenv("PORT"), handler);

    mg_set_protocol_http_websocket(conn);

    while (1)
    {
        mg_mgr_poll(&mgr, 1000);
    }
}
