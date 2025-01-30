#define _POSIX_C_SOURCE 200112L

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ui.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define HOST "irc.chat.twitch.tv"
#define PORT "6667"

uiWindow *w;
uiMultilineEntry *e;

static int onClosing(uiWindow *, void *) {
  uiQuit();
  return 1;
}

char buffer[1024] = {0};

static void runOnMainThread(void *arg) {
  char *text = (char *)arg;
  uiMultilineEntryAppend(e, text);
}

static void *ircThread(void *) {
  // create tcp socket and connect to server
  struct addrinfo hints = {0};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo *addrs;
  if (getaddrinfo(HOST, PORT, &hints, &addrs) < 0) {
    fprintf(stderr, "Could not get address of `" HOST "`: %s\n", strerror(errno));
    exit(1);
  }

  int sd = 0;
  for (struct addrinfo *addr = addrs; addr != NULL; addr = addr->ai_next) {
    sd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);

    if (sd == -1)
      break;
    if (connect(sd, addr->ai_addr, addr->ai_addrlen) == 0)
      break;

    close(sd);
    sd = -1;
  }
  freeaddrinfo(addrs);

  if (sd == -1) {
    fprintf(stderr, "Could not connect to " HOST ":" PORT ": %s\n", strerror(errno));
    exit(1);
  }

  char *requestCapabilitiesString = "CAP REQ :twitch.tv/commands\r\n";
  send(sd, requestCapabilitiesString, strlen(requestCapabilitiesString), 0);
  char oauth[1024];
  if (getenv("TWITCH_OAUTH") == NULL) {
    fprintf(stderr, "TWITCH_OAUTH environment variable not set\n");
    exit(1);
  }
  sprintf(oauth, "PASS oauth:%s\r\n", getenv("TWITCH_OAUTH"));
  send(sd, oauth, strlen(oauth), 0);
  char *requestNick = "NICK mohad12211\r\n";
  send(sd, requestNick, strlen(requestNick), 0);

  char buf[1024];

  while (1) {
    int n = recv(sd, buf, sizeof(buf), 0);
    if (n <= 0) {
      break;
    }
    buf[n] = '\0';
    uiQueueMain(runOnMainThread, strdup(buf));
    if (strstr(buf, "PING") == buf) {
      buf[1] = 'O';
      send(sd, buf, n, 0);
      printf("sent: %s\n", buf);
    }
  }

  return NULL;
}

int main(void) {
  uiInitOptions o = {0};
  const char *err;

  err = uiInit(&o);
  if (err != NULL) {
    fprintf(stderr, "Error initializing libui-ng: %s\n", err);
    uiFreeInitError(err);
    return 1;
  }

  w = uiNewWindow("IrChat", 1600, 900, 0);
  uiWindowOnClosing(w, onClosing, NULL);

  e = uiNewMultilineEntry();
  uiMultilineEntrySetReadOnly(e, true);

  uiBox *box = uiNewVerticalBox();
  uiBoxAppend(box, uiControl(e), true);
  uiWindowSetChild(w, uiControl(box));

  uiControlShow(uiControl(w));

  pthread_t thread;
  pthread_create(&thread, NULL, ircThread, NULL);
  pthread_detach(thread);

  uiMain();
  uiUninit();

  return 0;
}
