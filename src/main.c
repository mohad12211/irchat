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
#define MAX_MESSAGES 100

typedef struct {
  char username[64];
  char message[512];
} ChatMessage;

typedef struct {
  ChatMessage messages[MAX_MESSAGES];
  int messageCount;
  uiAttributedString *displayText;
  pthread_mutex_t mutex;
} ChatState;

uiWindow *w;
uiArea *chatArea;
uiAreaHandler handler;
uiEntry *entry;
uiButton *b;
int sd;
ChatState chatState;

static int onClosing(uiWindow *, void *) {
  uiQuit();
  return 1;
}

static void updateDisplayText(void) {
  pthread_mutex_lock(&chatState.mutex);

  if (chatState.displayText != NULL) {
    uiFreeAttributedString(chatState.displayText);
  }

  chatState.displayText = uiNewAttributedString("");

  for (int i = 0; i < chatState.messageCount; i++) {
    // Add username in bold blue
    uiAttribute *boldAttr = uiNewWeightAttribute(uiTextWeightBold);
    uiAttribute *usernameColor = uiNewColorAttribute(0.2, 0.4, 0.8, 1.0);

    size_t start = uiAttributedStringLen(chatState.displayText);
    size_t end;

    char userPrefix[128];
    snprintf(userPrefix, sizeof(userPrefix), "%s: ", chatState.messages[i].username);
    uiAttributedStringAppendUnattributed(chatState.displayText, userPrefix);

    end = uiAttributedStringLen(chatState.displayText);
    uiAttributedStringSetAttribute(chatState.displayText, boldAttr, start, end);
    uiAttributedStringSetAttribute(chatState.displayText, usernameColor, start, end);

    // Add message in white
    start = uiAttributedStringLen(chatState.displayText);
    uiAttributedStringAppendUnattributed(chatState.displayText, chatState.messages[i].message);
    end = uiAttributedStringLen(chatState.displayText);

    uiAttribute *messageColor = uiNewColorAttribute(1.0, 1.0, 1.0, 1.0); // White color
    uiAttributedStringSetAttribute(chatState.displayText, messageColor, start, end);

    uiAttributedStringAppendUnattributed(chatState.displayText, "\n");
  }

  pthread_mutex_unlock(&chatState.mutex);
}

static void addChatMessage(const char *username, const char *message) {
  pthread_mutex_lock(&chatState.mutex);

  if (chatState.messageCount >= MAX_MESSAGES) {
    memmove(&chatState.messages[0], &chatState.messages[1], sizeof(ChatMessage) * (MAX_MESSAGES - 1));
    chatState.messageCount--;
  }

  strncpy(chatState.messages[chatState.messageCount].username, username, 63);
  strncpy(chatState.messages[chatState.messageCount].message, message, 511);
  chatState.messages[chatState.messageCount].username[63] = '\0';
  chatState.messages[chatState.messageCount].message[511] = '\0';

  chatState.messageCount++;

  pthread_mutex_unlock(&chatState.mutex);

  updateDisplayText();
}

static void handlerDraw(uiAreaHandler *, uiArea *, uiAreaDrawParams *p) {
  uiDrawTextLayout *textLayout;
  uiFontDescriptor font;
  uiDrawTextLayoutParams params;

  pthread_mutex_lock(&chatState.mutex);

  if (chatState.displayText == NULL) {
    pthread_mutex_unlock(&chatState.mutex);
    return;
  }

  uiLoadControlFont(&font);
  font.Size = 16;

  params.String = chatState.displayText;
  params.DefaultFont = &font;
  params.Width = p->AreaWidth;
  params.Align = uiDrawTextAlignLeft;

  textLayout = uiDrawNewTextLayout(&params);
  uiDrawText(p->Context, textLayout, 0, 0);

  uiDrawFreeTextLayout(textLayout);
  uiFreeFontButtonFont(&font);

  pthread_mutex_unlock(&chatState.mutex);
}

static void handlerMouseEvent(uiAreaHandler *, uiArea *, uiAreaMouseEvent *) {}
static void handlerMouseCrossed(uiAreaHandler *, uiArea *, int) {}
static void handlerDragBroken(uiAreaHandler *, uiArea *) {}
static int handlerKeyEvent(uiAreaHandler *, uiArea *, uiAreaKeyEvent *) { return 0; }

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

static void redrawChat(void *) { uiAreaQueueRedrawAll(chatArea); }

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
      addChatMessage(username, message);
      uiQueueMain(redrawChat, NULL);
    }
  }

  return NULL;
}

static void onSendClicked(uiButton *, void *) {
  const char *text = uiEntryText(entry);
  char message[1024];
  sprintf(message, "PRIVMSG #mohad12211 :%s\r\n", text);
  send(sd, message, strlen(message), 0);
  addChatMessage("mohad12211", text);
  uiQueueMain(redrawChat, NULL);
  uiEntrySetText(entry, "");
}

int main(void) {
  uiInitOptions o = {0};
  const char *err = uiInit(&o);
  if (err != NULL) {
    fprintf(stderr, "Error initializing libui-ng: %s\n", err);
    uiFreeInitError(err);
    return 1;
  }

  // Initialize chat state
  memset(&chatState, 0, sizeof(ChatState));
  pthread_mutex_init(&chatState.mutex, NULL);

  // Setup handler
  handler.Draw = handlerDraw;
  handler.MouseEvent = handlerMouseEvent;
  handler.MouseCrossed = handlerMouseCrossed;
  handler.DragBroken = handlerDragBroken;
  handler.KeyEvent = handlerKeyEvent;

  w = uiNewWindow("IrChat", 1600, 900, 0);
  uiWindowOnClosing(w, onClosing, NULL);
  uiWindowSetMargined(w, 1);

  chatArea = uiNewArea(&handler);
  entry = uiNewEntry();
  b = uiNewButton("Send");

  uiButtonOnClicked(b, onSendClicked, NULL);

  uiBox *hbox = uiNewHorizontalBox();
  uiBoxAppend(hbox, uiControl(entry), 1);
  uiBoxAppend(hbox, uiControl(b), 0);

  uiBox *vbox = uiNewVerticalBox();
  uiBoxAppend(vbox, uiControl(chatArea), 1);
  uiBoxAppend(vbox, uiControl(hbox), 0);

  uiWindowSetChild(w, uiControl(vbox));
  uiControlShow(uiControl(w));

  pthread_t thread;
  pthread_create(&thread, NULL, ircThread, NULL);
  pthread_detach(thread);

  uiMain();

  pthread_mutex_destroy(&chatState.mutex);
  if (chatState.displayText != NULL) {
    uiFreeAttributedString(chatState.displayText);
  }

  uiUninit();
  return 0;
}
