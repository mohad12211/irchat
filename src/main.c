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
uiEntry *entry;
uiButton *b;
int sd;

// :<user>!<user>@<user>.tmi.twitch.tv PRIVMSG #<channel> :<message>\r\n
static void parse_twitch_message(const char *input, char *username, char *message) {
  const char *username_start = input + 1;
  const char *username_end = strchr(username_start, '!');
  size_t username_length = username_end - username_start;
  strncpy(username, username_start, username_length);
  username[username_length] = '\0';
  char *message_start = strchr(strstr(input, "PRIVMSG"), ':') + 1;
  // remove \r\n
  message_start[strlen(message_start) - 2] = '\0';
  strcpy(message, message_start);
}

static int onClosing(uiWindow *, void *) {
  uiQuit();
  return 1;
}

static void runOnMainThread(void *arg) {
  char *text = (char *)arg;
  uiMultilineEntryAppend(e, text);
  free(text);
}

static void *ircThread(void *) {
  struct addrinfo hints = {0};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo *addrs;
  if (getaddrinfo(HOST, PORT, &hints, &addrs) < 0) {
    fprintf(stderr, "Could not get address of `" HOST "`: %s\n", strerror(errno));
    exit(1);
  }

  sd = 0;
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
    if (strstr(buf, "PING") == buf) {
      buf[1] = 'O';
      send(sd, buf, n, 0);
      printf("sent: %s\n", buf);
    }
    if (strstr(buf, "GLOBALUSERSTATE")) {
      char *joinChannel = "JOIN #mohad12211\r\n";
      send(sd, joinChannel, strlen(joinChannel), 0);
    }
    if (strstr(buf, "PRIVMSG")) {
      char username[1024];
      char message[1024];
      parse_twitch_message(buf, username, message);
      char formattedMessage[1024];
      sprintf(formattedMessage, "%s: %s\n", username, message);
      uiQueueMain(runOnMainThread, strdup(formattedMessage));
    }
  }

  return NULL;
}

static void onSendClicked(uiButton *, void *) {
  const char *text = uiEntryText(entry);
  char message[1024];
  sprintf(message, "PRIVMSG #mohad12211 :%s\r\n", text);
  send(sd, message, strlen(message), 0);
  char formattedMessage[1024];
  sprintf(formattedMessage, "mohad12211: %s\n", text);
  uiQueueMain(runOnMainThread, strdup(formattedMessage));
  uiEntrySetText(entry, "");
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

  entry = uiNewEntry();
  b = uiNewButton("Send");

  uiButtonOnClicked(b, onSendClicked, NULL);

  uiBox *hbox = uiNewHorizontalBox();
  uiBoxAppend(hbox, uiControl(entry), true);
  uiBoxAppend(hbox, uiControl(b), false);

  uiBox *box = uiNewVerticalBox();
  uiBoxAppend(box, uiControl(e), true);
  uiBoxAppend(box, uiControl(hbox), false);
  uiWindowSetChild(w, uiControl(box));

  uiControlShow(uiControl(w));

  pthread_t thread;
  pthread_create(&thread, NULL, ircThread, NULL);
  pthread_detach(thread);

  uiMain();
  uiUninit();

  return 0;
}
