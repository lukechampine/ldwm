/* See LICENSE file for copyright and license details.
 *
 * less dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance.  Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of ldwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag.  Clients are organized in a linked client
 * list, and the focus history is remembered through a stack list. Each client 
 * contains a bit array to indicate the tags of a client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */
#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

/* macros */
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define ISVISIBLE(C)            ((C->tags & mons->tagset[mons->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define MAXCOLORS               12
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (textnw(X, strlen(X)) + dc.font.height)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast };        /* cursor */
enum { ColFG, ColBG, ColLast };                         /* color */
enum { NetSupported, NetWMName, NetWMState,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast };     /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast };             /* clicks */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	unsigned int tags;
	Bool isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen;
	Client *next;
	Client *snext;
	Window win;
};

typedef struct {
	int x, y, w, h;
	unsigned long colors[MAXCOLORS][2];
 	Drawable drawable;
	GC gc;
	struct {
		int ascent;
		int descent;
		int height;
		XFontSet set;
		XFontStruct *xfont;
	} font;
} DC; /* draw context */

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(void);
} Layout;

typedef struct Pertag Pertag;
struct Monitor {
	char ltsymbol[16];
	float mfact;
	int nmaster;
	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	Bool showbar;
	Bool topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Window barwin;
	const Layout *lt[2];
    Pertag *pertag;
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	Bool isfloating;
} Rule;

/* function declarations */
static void applyrules(Client *c);
static Bool applysizehints(Client *c, int *x, int *y, int *w, int *h, Bool interact);
static void arrange(void);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void checkotherwm(void);
static void cleanup(void);
static void clearurgent(Client *c);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon(void);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static void die(const char *errstr, ...);
static void drawbar(void);
static void drawcoloredtext(char *text);
static void drawsquare(Bool filled, Bool empty, unsigned long col[ColLast]);
static void drawtext(const char *text, unsigned long col[ColLast], Bool pad);
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusstack(const Arg *arg);
static unsigned long getcolor(const char *colstr);
static Bool getrootptr(int *x, int *y);
static long getstate(Window w);
static Bool gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, Bool focused);
static void grabkeys(void);
static void incnmaster(const Arg *arg);
static void initfont(const char *fontstr);
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void monocle(void);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *);
static void propertynotify(XEvent *e);
static void quit(const Arg *arg);
static void resize(Client *c, int x, int y, int w, int h, Bool interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(void);
static void run(void);
static void scan(void);
static Bool sendevent(Client *c, Atom proto);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, Bool fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void showhide(Client *c);
static void sigchld(int unused);
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static int textnw(const char *text, unsigned int len);
static void tile(void);
static void tilegap(void);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, Bool setfocus);
static void unmanage(Client *c, Bool destroyed);
static void unmapnotify(XEvent *e);
static Bool updategeom(void);
static void updatebarpos(void);
static void updatebars(void);
static void updateclientlist(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatewindowtype(Client *c);
static void updatetitle(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static int xerror(Display *dpy, XErrorEvent *ee);
static int xerrordummy(Display *dpy, XErrorEvent *ee);
static int xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh, blw = 0;      /* bar geometry */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
	[ButtonPress] = buttonpress,
	[ClientMessage] = clientmessage,
	[ConfigureRequest] = configurerequest,
	[ConfigureNotify] = configurenotify,
	[DestroyNotify] = destroynotify,
	[EnterNotify] = enternotify,
	[Expose] = expose,
	[FocusIn] = focusin,
	[KeyPress] = keypress,
	[MappingNotify] = mappingnotify,
	[MapRequest] = maprequest,
	[PropertyNotify] = propertynotify,
	[UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static Bool running = True;
static Cursor cursor[CurLast];
static Display *dpy;
static DC dc;
unsigned long bcolors[2];
unsigned long tcolors[2][ColLast];
unsigned long scolors[MAXCOLORS][ColLast];
static Monitor *mons = NULL;
static Window root;

/* configuration, allows nested code to access above variables */
#include "config.h"

struct Pertag {
   unsigned int curtag, prevtag; /* current and previous tag */
   int nmasters[LENGTH(tags) + 1]; /* number of windows in master area */
   float mfacts[LENGTH(tags) + 1]; /* mfacts per tag */
   unsigned int sellts[LENGTH(tags) + 1]; /* selected layouts */
   const Layout *ltidxs[LENGTH(tags) + 1][2]; /* matrix of tags and layouts indexes  */
};


/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
void
applyrules(Client *c) {
	const char *class, *instance;
	unsigned int i;
	const Rule *r;
	XClassHint ch = { NULL, NULL };

	/* rule matching */
	c->isfloating = c->tags = 0;
	XGetClassHint(dpy, c->win, &ch);
	class    = ch.res_class ? ch.res_class : broken;
	instance = ch.res_name  ? ch.res_name  : broken;

	for(i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if((!r->title || strstr(c->name, r->title))
		&& (!r->class || strstr(class, r->class))
		&& (!r->instance || strstr(instance, r->instance)))
		{
			c->isfloating = r->isfloating;
			c->tags |= r->tags;
		}
	}
	if(ch.res_class)
		XFree(ch.res_class);
	if(ch.res_name)
		XFree(ch.res_name);
	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : mons->tagset[mons->seltags];
}

Bool
applysizehints(Client *c, int *x, int *y, int *w, int *h, Bool interact) {
	Bool baseismin;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if(interact) {
		if(*x > sw)
			*x = sw - WIDTH(c);
		if(*y > sh)
			*y = sh - HEIGHT(c);
		if(*x + *w + 2 * c->bw < 0)
			*x = 0;
		if(*y + *h + 2 * c->bw < 0)
			*y = 0;
	}
	else {
		if(*x >= mons->wx + mons->ww)
			*x = mons->wx + mons->ww - WIDTH(c);
		if(*y >= mons->wy + mons->wh)
			*y = mons->wy + mons->wh - HEIGHT(c);
		if(*x + *w + 2 * c->bw <= mons->wx)
			*x = mons->wx;
		if(*y + *h + 2 * c->bw <= mons->wy)
			*y = mons->wy;
	}
	if(*h < bh)
		*h = bh;
	if(*w < bh)
		*w = bh;
	if(resizehints || c->isfloating || !mons->lt[mons->sellt]->arrange) {
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if(!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if(c->mina > 0 && c->maxa > 0) {
			if(c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if(c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if(baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if(c->incw)
			*w -= *w % c->incw;
		if(c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w = MAX(*w + c->basew, c->minw);
		*h = MAX(*h + c->baseh, c->minh);
		if(c->maxw)
			*w = MIN(*w, c->maxw);
		if(c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void
arrange(void) {
	if(mons) {
		showhide(mons->stack);
    	strncpy(mons->ltsymbol, mons->lt[mons->sellt]->symbol, sizeof mons->ltsymbol);
    	if(mons->lt[mons->sellt]->arrange)
    		mons->lt[mons->sellt]->arrange();
		restack();
    }
}

void
attach(Client *c) {
	c->next = mons->clients;
	mons->clients = c;
}

void
attachstack(Client *c) {
	c->snext = mons->stack;
	mons->stack = c;
}

void
buttonpress(XEvent *e) {
	unsigned int i, x, click;
	Arg arg = {0};
	Client *c;
	XButtonPressedEvent *ev = &e->xbutton;
fprintf(stderr,"button clicked");

	click = ClkRootWin;
	if(ev->window == mons->barwin) {
		i = x = 0;
		do
			x += TEXTW(tags[i]);
		while(ev->x >= x && ++i < LENGTH(tags));
		if(i < LENGTH(tags)) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		}
		else if(ev->x < x + blw)
			click = ClkLtSymbol;
		else if(ev->x > mons->ww - TEXTW(stext))
			click = ClkStatusText;
		else
			click = ClkWinTitle;
	}
	else if((c = wintoclient(ev->window))) {
fprintf(stderr,"window clicked");
		focus(c);
		click = ClkClientWin;
	}
	for(i = 0; i < LENGTH(buttons); i++)
		if(click == buttons[i].click && buttons[i].func && buttons[i].button == ev->button
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(ev->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);
}

void
checkotherwm(void) {
	xerrorxlib = XSetErrorHandler(xerrorstart);
	/* this causes an error if some other window manager is running */
	XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
	XSync(dpy, False);
	XSetErrorHandler(xerror);
	XSync(dpy, False);
}

void
cleanup(void) {
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };

	view(&a);
	mons->lt[mons->sellt] = &foo;
	while(mons->stack)
		unmanage(mons->stack, False);
	if(dc.font.set)
		XFreeFontSet(dpy, dc.font.set);
	else
		XFreeFont(dpy, dc.font.xfont);
	XUngrabKey(dpy, AnyKey, AnyModifier, root);
	XFreePixmap(dpy, dc.drawable);
	XFreeGC(dpy, dc.gc);
	XFreeCursor(dpy, cursor[CurNormal]);
	XFreeCursor(dpy, cursor[CurResize]);
	XFreeCursor(dpy, cursor[CurMove]);
	XUnmapWindow(dpy, mons->barwin);
	XDestroyWindow(dpy, mons->barwin);
	free(mons);
	XSync(dpy, False);
	XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
	XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}

void
clearurgent(Client *c) {
	XWMHints *wmh;

	c->isurgent = False;
	if(!(wmh = XGetWMHints(dpy, c->win)))
		return;
	wmh->flags &= ~XUrgencyHint;
	XSetWMHints(dpy, c->win, wmh);
	XFree(wmh);
}

void
clientmessage(XEvent *e) {
	XClientMessageEvent *cme = &e->xclient;
	Client *c = wintoclient(cme->window);

	if(!c)
		return;
	if(cme->message_type == netatom[NetWMState]) {
		if(cme->data.l[1] == netatom[NetWMFullscreen] || cme->data.l[2] == netatom[NetWMFullscreen])
			setfullscreen(c, (cme->data.l[0] == 1 /* _NET_WM_STATE_ADD    */
			              || (cme->data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c->isfullscreen)));
	}
	else if(cme->message_type == netatom[NetActiveWindow]) {
		if(!ISVISIBLE(c)) {
			mons->seltags ^= 1;
			mons->tagset[mons->seltags] = c->tags;
		}
		pop(c);
	}
}

void
configure(Client *c) {
	XConfigureEvent ce;

	ce.type = ConfigureNotify;
	ce.display = dpy;
	ce.event = c->win;
	ce.window = c->win;
	ce.x = c->x;
	ce.y = c->y;
	ce.width = c->w;
	ce.height = c->h;
	ce.border_width = c->bw;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(dpy, c->win, False, StructureNotifyMask, (XEvent *)&ce);
}

void
configurenotify(XEvent *e) {
	XConfigureEvent *ev = &e->xconfigure;
	Bool dirty;

	// TODO: updategeom handling sucks, needs to be simplified
	if(ev->window == root) {
		dirty = (sw != ev->width || sh != ev->height);
		sw = ev->width;
		sh = ev->height;
		if(updategeom() || dirty) {
			if(dc.drawable != 0)
				XFreePixmap(dpy, dc.drawable);
			dc.drawable = XCreatePixmap(dpy, root, sw, bh, DefaultDepth(dpy, screen));
			updatebars();
			XMoveResizeWindow(dpy, mons->barwin, mons->wx, mons->by, mons->ww, bh);
			focus(NULL);
			arrange();
		}
	}
}

void
configurerequest(XEvent *e) {
	Client *c;
	XConfigureRequestEvent *ev = &e->xconfigurerequest;
	XWindowChanges wc;

	if((c = wintoclient(ev->window))) {
		if(ev->value_mask & CWBorderWidth)
			c->bw = ev->border_width;
		else if(c->isfloating || !mons->lt[mons->sellt]->arrange) {
			if(ev->value_mask & CWX) {
				c->oldx = c->x;
				c->x = mons->mx + ev->x;
			}
			if(ev->value_mask & CWY) {
				c->oldy = c->y;
				c->y = mons->my + ev->y;
			}
			if(ev->value_mask & CWWidth) {
				c->oldw = c->w;
				c->w = ev->width;
			}
			if(ev->value_mask & CWHeight) {
				c->oldh = c->h;
				c->h = ev->height;
			}
			if((c->x + c->w) > mons->mx + mons->mw && c->isfloating)
				c->x = mons->mx + (mons->mw / 2 - WIDTH(c) / 2); /* center in x direction */
			if((c->y + c->h) > mons->my + mons->mh && c->isfloating)
				c->y = mons->my + (mons->mh / 2 - HEIGHT(c) / 2); /* center in y direction */
			if((ev->value_mask & (CWX|CWY)) && !(ev->value_mask & (CWWidth|CWHeight)))
				configure(c);
			if(ISVISIBLE(c))
				XMoveResizeWindow(dpy, c->win, c->x, c->y, c->w, c->h);
		}
		else
			configure(c);
	}
	else {
		wc.x = ev->x;
		wc.y = ev->y;
		wc.width = ev->width;
		wc.height = ev->height;
		wc.border_width = ev->border_width;
		wc.sibling = ev->above;
		wc.stack_mode = ev->detail;
		XConfigureWindow(dpy, ev->window, ev->value_mask, &wc);
	}
	XSync(dpy, False);
}

Monitor *
createmon(void) {
	Monitor *m;
    int i;

	if(!(m = (Monitor *)calloc(1, sizeof(Monitor))))
		die("fatal: could not malloc() %u bytes\n", sizeof(Monitor));
    if(!(m->pertag = (Pertag *)calloc(1, sizeof(Pertag))))
       die("fatal: could not malloc() %u bytes\n", sizeof(Pertag));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->showbar = showbar;
	m->topbar = topbar;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
    m->pertag->curtag = m->pertag->prevtag = 1;
    for(i=0; i <= LENGTH(tags); i++) {
        m->pertag->nmasters[i] = m->nmaster;
        m->pertag->mfacts[i] = m->mfact;
        m->pertag->ltidxs[i][0] = m->lt[0];
        m->pertag->ltidxs[i][1] = m->lt[1];
        m->pertag->sellts[i] = m->sellt;
    }
	return m;
}

void
destroynotify(XEvent *e) {
	Client *c;
	XDestroyWindowEvent *ev = &e->xdestroywindow;

	if((c = wintoclient(ev->window)))
		unmanage(c, True);
}

void
detach(Client *c) {
	Client **tc;

	for(tc = &mons->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void
detachstack(Client *c) {
	Client **tc, *t;

	for(tc = &mons->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if(c == mons->sel) {
		for(t = mons->stack; t && !ISVISIBLE(t); t = t->snext);
		mons->sel = t;
	}
}

void
die(const char *errstr, ...) {
    va_list ap;

    va_start(ap, errstr);
    vfprintf(stderr, errstr, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}


void
drawbar(void) {
	int x;
	unsigned int i, occ = 0, urg = 0;
	unsigned long *col;
	Client *c;

	for(c = mons->clients; c; c = c->next) {
		occ |= c->tags;
		if(c->isurgent)
			urg |= c->tags;
	}
	dc.x = 0;
	for(i = 0; i < LENGTH(tags); i++) {
		dc.w = TEXTW(tags[i]);
		col = tcolors[ (mons->tagset[mons->seltags] & 1 << i ? 1:(urg & 1 << i ? 2:0))];
		drawtext(tags[i], col, True);
		drawsquare(mons->sel && mons->sel->tags & 1 << i, occ & 1 << i, col);
		dc.x += dc.w;
	}
	dc.w = blw = TEXTW(mons->ltsymbol);
	drawtext(mons->ltsymbol, tcolors[0], True);
	dc.x += dc.w;
	x = dc.x;
	dc.w = textnw(stext, strlen(stext));
	dc.x = mons->ww - dc.w;
	if(dc.x < x) {
		dc.x = x;
		dc.w = mons->ww - x;
	}
	drawcoloredtext(stext);
	if((dc.w = dc.x - x) > bh) {
		dc.x = x;
		if(mons->sel) {
			col = tcolors[1];
			drawtext(mons->sel->name, col, True);
			drawsquare(mons->sel->isfixed, mons->sel->isfloating, col);
		}
		else
			drawtext(NULL, tcolors[0], False);
	}
	XCopyArea(dpy, dc.drawable, mons->barwin, dc.gc, 0, 0, mons->ww, bh, 0, 0);
	XSync(dpy, False);
}

void
drawcoloredtext(char *text) {
	char *buf = text, *ptr = buf, c = 1;
	unsigned long *col = scolors[0];
	int i, ox = dc.x;

	while( *ptr ) {
		for( i = 0; *ptr < 0 || *ptr > NUMCOLORS; i++, ptr++);
		if( !*ptr ) break;
		c=*ptr;
		*ptr=0;
		if( i ) {
			dc.w = mons->ww - dc.x;
			drawtext(buf, col, False);
			dc.x += textnw(buf, i);
        }
		*ptr = c;
		col = scolors[ c-1 ];
		buf = ++ptr;
	}
	drawtext(buf, col, False);
	dc.x = ox;
}

void
drawsquare(Bool filled, Bool empty, unsigned long col[ColLast]) {
	int x;
	XSetForeground(dpy, dc.gc, col[ ColFG ]);
	x = (dc.font.ascent + dc.font.descent + 2) / 4;
	if(filled)
		XFillRectangle(dpy, dc.drawable, dc.gc, dc.x+1, dc.y+1, x+1, x+1);
	else if(empty)
		XDrawRectangle(dpy, dc.drawable, dc.gc, dc.x+1, dc.y+1, x, x);
}

void
drawtext(const char *text, unsigned long col[ColLast], Bool pad) {
	char buf[256];
	int i, x, y, h, len, olen;

	XSetForeground(dpy, dc.gc, col[ ColBG ]);
	XFillRectangle(dpy, dc.drawable, dc.gc, dc.x, dc.y, dc.w, dc.h);
	if(!text)
		return;
	olen = strlen(text);
	h = pad ? (dc.font.ascent + dc.font.descent) : 0;
	y = dc.y + ((dc.h + dc.font.ascent - dc.font.descent) / 2);
	x = dc.x + (h / 2);
	/* shorten text if necessary */
	for(len = MIN(olen, sizeof buf); len && textnw(text, len) > dc.w - h; len--);
	if(!len)
		return;
	memcpy(buf, text, len);
	if(len < olen)
		for(i = len; i && i > len - 3; buf[--i] = '.');
	XSetForeground(dpy, dc.gc, col[ ColFG ]);
	if(dc.font.set)
		XmbDrawString(dpy, dc.drawable, dc.font.set, dc.gc, x, y, buf, len);
	else
		XDrawString(dpy, dc.drawable, dc.gc, x, y, buf, len);
}


void
enternotify(XEvent *e) {
    XCrossingEvent *ev = &e->xcrossing;
    Client *c = wintoclient(ev->window);
fprintf(stderr,"enternotify\n");
    if((ev->mode != NotifyNormal || ev->detail == NotifyInferior) && ev->window != root)
        return;
    else if(!c || c == mons->sel)
        return;
    else
        focus(c);
}

void
expose(XEvent *e) {
    XExposeEvent *ev = &e->xexpose;
    
    if(ev->count == 0)
        drawbar();
}

void
focus(Client *c) {
	if(!c || !ISVISIBLE(c))
		for(c = mons->stack; c && !ISVISIBLE(c); c = c->snext);
	/* was if(selmon->sel) */
	if(mons->sel && mons->sel != c)
		unfocus(mons->sel, False);
	if(c) {
		if(c->isurgent)
			clearurgent(c);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, True);
		XSetWindowBorder(dpy, c->win, bcolors[0]);
		setfocus(c);
	}
	else {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
	mons->sel = c;
    drawbar();
}

void
focusin(XEvent *e) { /* there are some broken focus acquiring clients */
	XFocusChangeEvent *ev = &e->xfocus;

	if(mons->sel && ev->window != mons->sel->win)
		setfocus(mons->sel);
}

void
focusstack(const Arg *arg) {
	Client *c = NULL, *i;

	if(!mons->sel)
		return;
	if(arg->i > 0) {
		for(c = mons->sel->next; c && !ISVISIBLE(c); c = c->next);
		if(!c)
			for(c = mons->clients; c && !ISVISIBLE(c); c = c->next);
	}
	else {
		for(i = mons->clients; i != mons->sel; i = i->next)
			if(ISVISIBLE(i))
				c = i;
		if(!c)
			for(; i; i = i->next)
				if(ISVISIBLE(i))
					c = i;
	}
	if(c) {
		focus(c);
		restack();
	}
}

Atom
getatomprop(Client *c, Atom prop) {
	int di;
	unsigned long dl;
	unsigned char *p = NULL;
	Atom da, atom = None;

	if(XGetWindowProperty(dpy, c->win, prop, 0L, sizeof atom, False, XA_ATOM,
	                      &da, &di, &dl, &dl, &p) == Success && p) {
		atom = *(Atom *)p;
		XFree(p);
	}
	return atom;
}

unsigned long
getcolor(const char *colstr) {
	Colormap cmap = DefaultColormap(dpy, screen);
	XColor color;

	if(!XAllocNamedColor(dpy, cmap, colstr, &color, &color))
		die("error, cannot allocate color '%s'\n", colstr);
	return color.pixel;
}

Bool
getrootptr(int *x, int *y) {
	int di;
	unsigned int dui;
	Window dummy;

	return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}

long
getstate(Window w) {
	int format;
	long result = -1;
	unsigned char *p = NULL;
	unsigned long n, extra;
	Atom real;

	if(XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
	                      &real, &format, &n, &extra, (unsigned char **)&p) != Success)
		return -1;
	if(n != 0)
		result = *p;
	XFree(p);
	return result;
}

Bool
gettextprop(Window w, Atom atom, char *text, unsigned int size) {
	char **list = NULL;
	int n;
	XTextProperty name;

	if(!text || size == 0)
		return False;
	text[0] = '\0';
	XGetTextProperty(dpy, w, &name, atom);
	if(!name.nitems)
		return False;
	if(name.encoding == XA_STRING)
		strncpy(text, (char *)name.value, size - 1);
	else {
		if(XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
			strncpy(text, *list, size - 1);
			XFreeStringList(list);
		}
	}
	text[size - 1] = '\0';
	XFree(name.value);
	return True;
}

void
grabbuttons(Client *c, Bool focused) {
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		if(focused) {
			for(i = 0; i < LENGTH(buttons); i++)
				if(buttons[i].click == ClkClientWin)
					for(j = 0; j < LENGTH(modifiers); j++)
						XGrabButton(dpy, buttons[i].button,
						            buttons[i].mask | modifiers[j],
						            c->win, False, BUTTONMASK,
						            GrabModeAsync, GrabModeSync, None, None);
		}
		else
			XGrabButton(dpy, AnyButton, AnyModifier, c->win, False,
			            BUTTONMASK, GrabModeAsync, GrabModeSync, None, None);
	}
}

void
grabkeys(void) {
	updatenumlockmask();
	{
		unsigned int i, j;
		unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
		KeyCode code;

		XUngrabKey(dpy, AnyKey, AnyModifier, root);
		for(i = 0; i < LENGTH(keys); i++)
			if((code = XKeysymToKeycode(dpy, keys[i].keysym)))
				for(j = 0; j < LENGTH(modifiers); j++)
					XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
						 True, GrabModeAsync, GrabModeAsync);
	}
}

void
incnmaster(const Arg *arg) {
    mons->nmaster = mons->pertag->nmasters[mons->pertag->curtag] = MAX(mons->nmaster + arg->i, 0);
	arrange();
}

void
initfont(const char *fontstr) {
	char *def, **missing;
	int n;

	dc.font.set = XCreateFontSet(dpy, fontstr, &missing, &n, &def);
	if(missing) {
        n=0;
		XFreeStringList(missing);
	}
	if(dc.font.set) {
		XFontStruct **xfonts;
		char **font_names;

		dc.font.ascent = dc.font.descent = 0;
		XExtentsOfFontSet(dc.font.set);
		n = XFontsOfFontSet(dc.font.set, &xfonts, &font_names);
		while(n--) {
			dc.font.ascent = MAX(dc.font.ascent, (*xfonts)->ascent);
			dc.font.descent = MAX(dc.font.descent,(*xfonts)->descent);
			xfonts++;
		}
	}
	else {
		if(!(dc.font.xfont = XLoadQueryFont(dpy, fontstr))
		&& !(dc.font.xfont = XLoadQueryFont(dpy, "fixed")))
			die("error, cannot load font: '%s'\n", fontstr);
		dc.font.ascent = dc.font.xfont->ascent;
		dc.font.descent = dc.font.xfont->descent;
	}
	dc.font.height = dc.font.ascent + dc.font.descent;
}

void
keypress(XEvent *e) {
	unsigned int i;
	KeySym keysym;
	XKeyEvent *ev;

	ev = &e->xkey;
	keysym = XKeycodeToKeysym(dpy, (KeyCode)ev->keycode, 0);
	for(i = 0; i < LENGTH(keys); i++)
		if(keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
			keys[i].func(&(keys[i].arg));
}

void
killclient(const Arg *arg) {
	if(!mons->sel)
		return;
	if(!sendevent(mons->sel, wmatom[WMDelete])) {
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XSetCloseDownMode(dpy, DestroyAll);
		XKillClient(dpy, mons->sel->win);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
}

void
manage(Window w, XWindowAttributes *wa) {
	Client *c = NULL;
	Window trans = None;
	XWindowChanges wc;

	if(!(c = calloc(1, sizeof(Client))))
		die("fatal: could not malloc() %u bytes\n", sizeof(Client));
	c->win = w;
	updatetitle(c);
	applyrules(c);

	/* geometry */
	c->x = c->oldx = wa->x;
	c->y = c->oldy = wa->y;
	c->w = c->oldw = wa->width;
	c->h = c->oldh = wa->height;
	c->oldbw = wa->border_width;

	if(c->x + WIDTH(c) > mons->mx + mons->mw)
		c->x = mons->mx + mons->mw - WIDTH(c);
	if(c->y + HEIGHT(c) > mons->my + mons->mh)
		c->y = mons->my + mons->mh - HEIGHT(c);
	c->x = MAX(c->x, mons->mx);
	/* only fix client y-offset, if the client center might cover the bar */
	c->y = MAX(c->y, ((mons->by == mons->my) && (c->x + (c->w / 2) >= mons->wx)
	           && (c->x + (c->w / 2) < mons->wx + mons->ww)) ? bh : mons->my);
	c->bw = borderpx;

	wc.border_width = c->bw;
	XConfigureWindow(dpy, w, CWBorderWidth, &wc);
	XSetWindowBorder(dpy, w, bcolors[0]);
	configure(c); /* propagates border_width, if size doesn't change */
	updatewindowtype(c);
	updatesizehints(c);
	updatewmhints(c);
	XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
	grabbuttons(c, False);
	if(!c->isfloating)
		c->isfloating = c->oldstate = trans != None || c->isfixed;
	if(c->isfloating)
		XRaiseWindow(dpy, c->win);
	attach(c);
	attachstack(c);
	XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
	                (unsigned char *) &(c->win), 1);
	XMoveResizeWindow(dpy, c->win, c->x + 2 * sw, c->y, c->w, c->h); /* some windows require this */
	setclientstate(c, NormalState);
    unfocus(mons->sel, False);
	mons->sel = c;
	arrange();
	XMapWindow(dpy, c->win);
	focus(NULL);
}

void
mappingnotify(XEvent *e) {
	XMappingEvent *ev = &e->xmapping;

	XRefreshKeyboardMapping(ev);
	if(ev->request == MappingKeyboard)
		grabkeys();
}

void
maprequest(XEvent *e) {
	static XWindowAttributes wa;
	XMapRequestEvent *ev = &e->xmaprequest;

	if(!XGetWindowAttributes(dpy, ev->window, &wa))
		return;
	if(wa.override_redirect)
		return;
	if(!wintoclient(ev->window))
		manage(ev->window, &wa);
}

void
monocle(void) {
	unsigned int n = 0;
	Client *c;

	for(c = mons->clients; c; c = c->next)
		if(ISVISIBLE(c))
			n++;
	if(n > 0) /* override layout symbol */
		snprintf(mons->ltsymbol, sizeof mons->ltsymbol, "[%d]", n);
	for(c = nexttiled(mons->clients); c; c = nexttiled(c->next))
		resize(c, mons->wx, mons->wy, mons->ww - 2 * c->bw, mons->wh - 2 * c->bw, False);
}

void
movemouse(const Arg *arg) {
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	XEvent ev;

	if(!(c = mons->sel))
		return;
	if(c->isfullscreen) /* no support moving fullscreen windows by mouse */
		return;
	restack();
	ocx = c->x;
	ocy = c->y;
	if(XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
	None, cursor[CurMove], CurrentTime) != GrabSuccess)
		return;
	if(!getrootptr(&x, &y))
		return;
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			nx = ocx + (ev.xmotion.x - x);
			ny = ocy + (ev.xmotion.y - y);
			if(nx >= mons->wx && nx <= mons->wx + mons->ww
			&& ny >= mons->wy && ny <= mons->wy + mons->wh) {
				if(abs(mons->wx - nx) < snap)
					nx = mons->wx;
				else if(abs((mons->wx + mons->ww) - (nx + WIDTH(c))) < snap)
					nx = mons->wx + mons->ww - WIDTH(c);
				if(abs(mons->wy - ny) < snap)
					ny = mons->wy;
				else if(abs((mons->wy + mons->wh) - (ny + HEIGHT(c))) < snap)
					ny = mons->wy + mons->wh - HEIGHT(c);
				if(!c->isfloating && mons->lt[mons->sellt]->arrange
				&& (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
					togglefloating(NULL);
			}
			if(!mons->lt[mons->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, True);
			break;
		}
	} while(ev.type != ButtonRelease);
	XUngrabPointer(dpy, CurrentTime);
}

Client *
nexttiled(Client *c) {
	for(; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

void
pop(Client *c) {
	detach(c);
	attach(c);
	focus(c);
	arrange();
}

void
propertynotify(XEvent *e) {
	Client *c;
	Window trans;
	XPropertyEvent *ev = &e->xproperty;

	if((ev->window == root) && (ev->atom == XA_WM_NAME))
		updatestatus();
	else if(ev->state == PropertyDelete)
		return; /* ignore */
	else if((c = wintoclient(ev->window))) {
		switch(ev->atom) {
		default: break;
		case XA_WM_TRANSIENT_FOR:
			if(!c->isfloating && (XGetTransientForHint(dpy, c->win, &trans)) &&
			   (c->isfloating = (wintoclient(trans)) != NULL))
				arrange();
			break;
		case XA_WM_NORMAL_HINTS:
			updatesizehints(c);
			break;
		case XA_WM_HINTS:
			updatewmhints(c);
            drawbar();
			break;
		}
		if(ev->atom == XA_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if(c == mons->sel)
				drawbar();
		}
		if(ev->atom == netatom[NetWMWindowType])
			updatewindowtype(c);
	}
}

void
quit(const Arg *arg) {
	running = False;
}

void
resize(Client *c, int x, int y, int w, int h, Bool interact) {
	if(applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void
resizeclient(Client *c, int x, int y, int w, int h) {
	XWindowChanges wc;

	c->oldx = c->x; c->x = wc.x = x;
	c->oldy = c->y; c->y = wc.y = y;
	c->oldw = c->w; c->w = wc.width = w;
	c->oldh = c->h; c->h = wc.height = h;
	wc.border_width = c->bw;
	XConfigureWindow(dpy, c->win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
	configure(c);
	XSync(dpy, False);
}

void
resizemouse(const Arg *arg) {
	int ocx, ocy;
	int nw, nh;
	Client *c;
	XEvent ev;

	if(!(c = mons->sel))
		return;
	if(c->isfullscreen) /* no support resizing fullscreen windows by mouse */
		return;
	restack();
	ocx = c->x;
	ocy = c->y;
	if(XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
	                None, cursor[CurResize], CurrentTime) != GrabSuccess)
		return;
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	do {
		XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
		switch(ev.type) {
		case ConfigureRequest:
		case Expose:
		case MapRequest:
			handler[ev.type](&ev);
			break;
		case MotionNotify:
			nw = MAX(ev.xmotion.x - ocx - 2 * c->bw + 1, 1);
			nh = MAX(ev.xmotion.y - ocy - 2 * c->bw + 1, 1);
			if(mons->wx + nw >= mons->wx && mons->wx + nw <= mons->wx + mons->ww
			&& mons->wy + nh >= mons->wy && mons->wy + nh <= mons->wy + mons->wh)
			{
				if(!c->isfloating && mons->lt[mons->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
			}
			if(!mons->lt[mons->sellt]->arrange || c->isfloating)
				resize(c, c->x, c->y, nw, nh, True);
			break;
		}
	} while(ev.type != ButtonRelease);
	XWarpPointer(dpy, None, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	XUngrabPointer(dpy, CurrentTime);
	while(XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
restack(void) {
	Client *c;
	XEvent ev;
	XWindowChanges wc;

	drawbar();
	if(!mons->sel)
		return;
	if(mons->sel->isfloating || !mons->lt[mons->sellt]->arrange)
		XRaiseWindow(dpy, mons->sel->win);
	if(mons->lt[mons->sellt]->arrange) {
		wc.stack_mode = Below;
		wc.sibling = mons->barwin;
		for(c = mons->stack; c; c = c->snext)
			if(!c->isfloating && ISVISIBLE(c)) {
				XConfigureWindow(dpy, c->win, CWSibling|CWStackMode, &wc);
				wc.sibling = c->win;
			}
	}
	XSync(dpy, False);
	while(XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}

void
run(void) {
	XEvent ev;
	/* main event loop */
	XSync(dpy, False);
	while(running && !XNextEvent(dpy, &ev))
		if(handler[ev.type])
			handler[ev.type](&ev); /* call handler */
}

void
scan(void) {
	unsigned int i, num;
	Window d1, d2, *wins = NULL;
	XWindowAttributes wa;

	if(XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
		for(i = 0; i < num; i++) {
			if(!XGetWindowAttributes(dpy, wins[i], &wa)
			|| wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1))
				continue;
			if(wa.map_state == IsViewable || getstate(wins[i]) == IconicState)
				manage(wins[i], &wa);
		}
		for(i = 0; i < num; i++) { /* now the transients */
			if(!XGetWindowAttributes(dpy, wins[i], &wa))
				continue;
			if(XGetTransientForHint(dpy, wins[i], &d1)
			&& (wa.map_state == IsViewable || getstate(wins[i]) == IconicState))
				manage(wins[i], &wa);
		}
		if(wins)
			XFree(wins);
	}
}

void
setclientstate(Client *c, long state) {
	long data[] = { state, None };

	XChangeProperty(dpy, c->win, wmatom[WMState], wmatom[WMState], 32,
			PropModeReplace, (unsigned char *)data, 2);
}

Bool
sendevent(Client *c, Atom proto) {
	int n;
	Atom *protocols;
	Bool exists = False;
	XEvent ev;

	if(XGetWMProtocols(dpy, c->win, &protocols, &n)) {
		while(!exists && n--)
			exists = protocols[n] == proto;
		XFree(protocols);
	}
	if(exists) {
		ev.type = ClientMessage;
		ev.xclient.window = c->win;
		ev.xclient.message_type = wmatom[WMProtocols];
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = proto;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, c->win, False, NoEventMask, &ev);
	}
	return exists;
}

void
setfocus(Client *c) {
	if(!c->neverfocus) {
		XSetInputFocus(dpy, c->win, RevertToPointerRoot, CurrentTime);
		XChangeProperty(dpy, root, netatom[NetActiveWindow],
 		                XA_WINDOW, 32, PropModeReplace,
 		                (unsigned char *) &(c->win), 1);
	}
	sendevent(c, wmatom[WMTakeFocus]);
}

void
setfullscreen(Client *c, Bool fullscreen) {
	if(fullscreen) {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		                PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);
		c->isfullscreen = True;
		c->oldstate = c->isfloating;
		c->oldbw = c->bw;
		c->bw = 0;
		c->isfloating = True;
		resizeclient(c, mons->mx, mons->my, mons->mw, mons->mh);
		XRaiseWindow(dpy, c->win);
	}
	else {
		XChangeProperty(dpy, c->win, netatom[NetWMState], XA_ATOM, 32,
		                PropModeReplace, (unsigned char*)0, 0);
		c->isfullscreen = False;
		c->isfloating = c->oldstate;
		c->bw = c->oldbw;
		c->x = c->oldx;
		c->y = c->oldy;
		c->w = c->oldw;
		c->h = c->oldh;
		resizeclient(c, c->x, c->y, c->w, c->h);
		arrange();
	}
}

void
setlayout(const Arg *arg) {
    if(!arg || !arg->v || arg->v != mons->lt[mons->sellt]) {
        mons->pertag->sellts[mons->pertag->curtag] ^= 1;
        mons->sellt = mons->pertag->sellts[mons->pertag->curtag];
    }

	if(arg && arg->v)
        mons->pertag->ltidxs[mons->pertag->curtag][mons->sellt] = (Layout *)arg->v;
    mons->lt[mons->sellt] = mons->pertag->ltidxs[mons->pertag->curtag][mons->sellt];
	strncpy(mons->ltsymbol, mons->lt[mons->sellt]->symbol, sizeof mons->ltsymbol);
	if(mons->sel)
		arrange();
	else
		drawbar();
}

/* arg > 1.0 will set mfact absolutly */
void
setmfact(const Arg *arg) {
	float f;

	if(!arg || !mons->lt[mons->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + mons->mfact : arg->f - 1.0;
	if(f < 0.1 || f > 0.9)
		return;
    mons->mfact = mons->pertag->mfacts[mons->pertag->curtag] = f;
	arrange();
}

void
setup(void) {
	XSetWindowAttributes wa;

	/* clean up any zombies immediately */
	sigchld(0);

	/* init screen */
	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);
	initfont(font);
	sw = DisplayWidth(dpy, screen);
	sh = DisplayHeight(dpy, screen);
	bh = dc.h = dc.font.height + 2;
	updategeom();
	/* init atoms */
	wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
	wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
	wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
	netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
	netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
	netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
	netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
	netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
	netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	/* init cursors */
	cursor[CurNormal] = XCreateFontCursor(dpy, XC_left_ptr);
	cursor[CurResize] = XCreateFontCursor(dpy, XC_sizing);
	cursor[CurMove] = XCreateFontCursor(dpy, XC_fleur);
	/* init appearance */
    for(int i=0; i<2; i++) {
		bcolors[i] = getcolor( bordercolors[i] );

		tcolors[i][ColFG] = getcolor( tagcolors[i][ColFG] );
		tcolors[i][ColBG] = getcolor( tagcolors[i][ColBG] );
    }
	for(int i=0; i<NUMCOLORS; i++) {
		scolors[i][ColFG] = getcolor( statuscolors[i][ColFG] );
		scolors[i][ColBG] = getcolor( statuscolors[i][ColBG] );
	}
	dc.drawable = XCreatePixmap(dpy, root, DisplayWidth(dpy, screen), bh, DefaultDepth(dpy, screen));
	dc.gc = XCreateGC(dpy, root, 0, NULL);
	XSetLineAttributes(dpy, dc.gc, 1, LineSolid, CapButt, JoinMiter);
	if(!dc.font.set)
		XSetFont(dpy, dc.gc, dc.font.xfont->fid);
	/* init bars */
	updatebars();
	updatestatus();
	/* EWMH support per view */
	XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
			PropModeReplace, (unsigned char *) netatom, NetLast);
	XDeleteProperty(dpy, root, netatom[NetClientList]);
	/* select for events */
	wa.cursor = cursor[CurNormal];
	wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask|ButtonPressMask|PointerMotionMask
	                |EnterWindowMask|LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
	XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
	XSelectInput(dpy, root, wa.event_mask);
	grabkeys();
}

void
showhide(Client *c) {
	if(!c)
		return;
	if(ISVISIBLE(c)) { /* show clients top down */
		XMoveWindow(dpy, c->win, c->x, c->y);
		if((!mons->lt[mons->sellt]->arrange || c->isfloating) && !c->isfullscreen)
			resize(c, c->x, c->y, c->w, c->h, False);
		showhide(c->snext);
	}
	else { /* hide clients bottom up */
		showhide(c->snext);
		XMoveWindow(dpy, c->win, WIDTH(c) * -2, c->y);
	}
}

void
sigchld(int unused) {
	if(signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

void
spawn(const Arg *arg) {
	if(fork() == 0) {
		if(dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "ldwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(EXIT_SUCCESS);
	}
}

void
tag(const Arg *arg) {
	if(mons->sel && arg->ui & TAGMASK) {
		mons->sel->tags = arg->ui & TAGMASK;
		focus(NULL);
		arrange();
	}
}

int
textnw(const char *text, unsigned int len) {
    // remove non-printing characters before calculating width
    char *ptr = (char *) text;
    unsigned int i, ibuf, lenbuf=len;
    char buf[len+1];
	XRectangle r;

    for(i=0, ibuf=0; *ptr && i<len; i++, ptr++) {
        if (*ptr <= NUMCOLORS && *ptr > 0) {
            if (i < len)
                lenbuf--;
        }
        else {
            buf[ibuf]=*ptr;
            ibuf++;
        }
    }
    buf[ibuf]=0;

	if(dc.font.set) {
		XmbTextExtents(dc.font.set, buf, lenbuf, NULL, &r);
		return r.width;
	}
	return XTextWidth(dc.font.xfont, buf, lenbuf);
}

void
tile(void) {
	unsigned int i, n, h, mw, my, ty, numgaps;
	Client *c;

	for(n = 0, c = nexttiled(mons->clients); c; c = nexttiled(c->next), n++);
	if(n == 0)
		return;

	numgaps = (singlegap && n != 1) ? 1 : 2;

	if(n > mons->nmaster)
		mw = mons->nmaster ? mons->ww * mons->mfact : 0;
	else
		mw = mons->ww;
	for(i = my = ty = 0, c = nexttiled(mons->clients); c; c = nexttiled(c->next), i++)
		if(i < mons->nmaster) {
			h = (mons->wh - my) / (MIN(n, mons->nmaster) - i);
            resize(c, mons->wx, mons->wy + my, mw - numgaps*(c->bw), h - 2*(c->bw), False);
			my += HEIGHT(c);
		}
		else {
			h = (mons->wh - ty) / (n - i);
			resize(c, mons->wx + mw, mons->wy + ty,
                      mons->ww - mw - 2*(c->bw), h - 2*(c->bw), False);
			ty += HEIGHT(c) - (2-numgaps)*(c->bw);
		}
}

void
tilegap(void) {
	unsigned int i, n, h, mw, my, ty, numgaps;
	Client *c;

	for(n = 0, c = nexttiled(mons->clients); c; c = nexttiled(c->next), n++);
	if(n == 0)
		return;

	numgaps = (singlegap && n != 1) ? 1 : 2;

	if(n > mons->nmaster)
		mw = mons->nmaster ? mons->ww * mons->mfact : 0;
	else
		mw = mons->ww;
	for(i = my = ty = 0, c = nexttiled(mons->clients); c; c = nexttiled(c->next), i++)
		if(i < mons->nmaster) {
			h = (mons->wh - my) / (MIN(n, mons->nmaster) - i);
            resize(c, mons->wx + paddingpx, mons->wy + my + paddingpx,
                      mw - numgaps*(c->bw + paddingpx), h - 2*(c->bw + paddingpx), False);
			my += HEIGHT(c) + paddingpx;
		}
		else {
			h = (mons->wh - ty) / (n - i);
			resize(c, mons->wx + mw + paddingpx, mons->wy + ty + paddingpx,
                      mons->ww - mw - 2*(c->bw + paddingpx), h - 2*(c->bw + paddingpx), False);
			ty += HEIGHT(c) + numgaps*paddingpx - (2-numgaps)*(c->bw);
		}
}

void
togglebar(const Arg *arg) {
	mons->showbar = !mons->showbar;
	updatebarpos();
	XMoveResizeWindow(dpy, mons->barwin, mons->wx, mons->by, mons->ww, bh);
	arrange();
}

void
togglefloating(const Arg *arg) {
	if(!mons->sel)
		return;
	if(mons->sel->isfullscreen) /* no support for fullscreen windows */
		return;
	mons->sel->isfloating = !mons->sel->isfloating || mons->sel->isfixed;
	if(mons->sel->isfloating)
		resize(mons->sel, mons->sel->x, mons->sel->y,
		       mons->sel->w, mons->sel->h, False);
	arrange();
}

void
toggletag(const Arg *arg) {
	unsigned int newtags;

	if(!mons->sel)
		return;
	newtags = mons->sel->tags ^ (arg->ui & TAGMASK);
	if(newtags) {
		mons->sel->tags = newtags;
		focus(NULL);
		arrange();
	}
}

void
toggleview(const Arg *arg) {
	unsigned int newtagset = mons->tagset[mons->seltags] ^ (arg->ui & TAGMASK);
    int i;

	if(newtagset) {
        if(newtagset == ~0) {
            mons->pertag->prevtag = mons->pertag->curtag;
            mons->pertag->curtag = 0;
        }
        /* test if the user did not select the same tag */
        if(!(newtagset & 1 << (mons->pertag->curtag - 1))) {
            mons->pertag->prevtag = mons->pertag->curtag;
            for (i=0; !(newtagset & 1 << i); i++) ;
            mons->pertag->curtag = i + 1;
        }
		mons->tagset[mons->seltags] = newtagset;

        /* apply settings for this view */
        mons->nmaster = mons->pertag->nmasters[mons->pertag->curtag];
        mons->mfact = mons->pertag->mfacts[mons->pertag->curtag];
        mons->sellt = mons->pertag->sellts[mons->pertag->curtag];
        mons->lt[mons->sellt] = mons->pertag->ltidxs[mons->pertag->curtag][mons->sellt];
        mons->lt[mons->sellt^1] = mons->pertag->ltidxs[mons->pertag->curtag][mons->sellt^1];

		focus(NULL);
		arrange();
	}
}

void
unfocus(Client *c, Bool setfocus) {
	if(!c)
		return;
	grabbuttons(c, False);
	XSetWindowBorder(dpy, c->win, bcolors[1]);
	if(setfocus) {
		XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
		XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
	}
}

void
unmanage(Client *c, Bool destroyed) {
	XWindowChanges wc;

	/* The server grab construct avoids race conditions. */
	detach(c);
	detachstack(c);
	if(!destroyed) {
		wc.border_width = c->oldbw;
		XGrabServer(dpy);
		XSetErrorHandler(xerrordummy);
		XConfigureWindow(dpy, c->win, CWBorderWidth, &wc); /* restore border */
		XUngrabButton(dpy, AnyButton, AnyModifier, c->win);
		setclientstate(c, WithdrawnState);
		XSync(dpy, False);
		XSetErrorHandler(xerror);
		XUngrabServer(dpy);
	}
	free(c);
	focus(NULL);
	updateclientlist();
	arrange();
}

void
unmapnotify(XEvent *e) {
	Client *c;
	XUnmapEvent *ev = &e->xunmap;

	if((c = wintoclient(ev->window))) {
		if(ev->send_event)
			setclientstate(c, WithdrawnState);
		else
			unmanage(c, False);
	}
}

void
updatebars(void) {
	XSetWindowAttributes wa = {
		.override_redirect = True,
		.background_pixmap = ParentRelative,
		.event_mask = ButtonPressMask|ExposureMask
	};
	
    if (mons->barwin)
		return;
	mons->barwin = XCreateWindow(dpy, root, mons->wx, mons->by, mons->ww, bh, 0, DefaultDepth(dpy, screen),
	                          CopyFromParent, DefaultVisual(dpy, screen),
	                          CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);
	XDefineCursor(dpy, mons->barwin, cursor[CurNormal]);
	XMapRaised(dpy, mons->barwin);
}

void
updatebarpos(void) {
	mons->wy = mons->my;
	mons->wh = mons->mh;
	if(mons->showbar) {
		mons->wh -= bh;
		mons->by = mons->topbar ? mons->wy : mons->wy + mons->wh;
		mons->wy = mons->topbar ? mons->wy + bh : mons->wy;
	}
	else
		mons->by = -bh;
}

void
updateclientlist() {
	Client *c;

	XDeleteProperty(dpy, root, netatom[NetClientList]);
		for(c = mons->clients; c; c = c->next)
			XChangeProperty(dpy, root, netatom[NetClientList],
			                XA_WINDOW, 32, PropModeAppend,
			                (unsigned char *) &(c->win), 1);
}

Bool
updategeom(void) {
	Bool dirty = False;
	if(!mons)
		mons = createmon();
	if(mons->mw != sw || mons->mh != sh) {
		dirty = True;
		mons->mw = mons->ww = sw;
		mons->mh = mons->wh = sh;
		updatebarpos();
	}
	return dirty;
}

void
updatenumlockmask(void) {
	unsigned int i, j;
	XModifierKeymap *modmap;

	numlockmask = 0;
	modmap = XGetModifierMapping(dpy);
	for(i = 0; i < 8; i++)
		for(j = 0; j < modmap->max_keypermod; j++)
			if(modmap->modifiermap[i * modmap->max_keypermod + j]
			   == XKeysymToKeycode(dpy, XK_Num_Lock))
				numlockmask = (1 << i);
	XFreeModifiermap(modmap);
}

void
updatesizehints(Client *c) {
	long msize;
	XSizeHints size;

	if(!XGetWMNormalHints(dpy, c->win, &size, &msize))
		/* size is uninitialized, ensure that size.flags aren't used */
		size.flags = PSize;
	if(size.flags & PBaseSize) {
		c->basew = size.base_width;
		c->baseh = size.base_height;
	}
	else if(size.flags & PMinSize) {
		c->basew = size.min_width;
		c->baseh = size.min_height;
	}
	else
		c->basew = c->baseh = 0;
	if(size.flags & PResizeInc) {
		c->incw = size.width_inc;
		c->inch = size.height_inc;
	}
	else
		c->incw = c->inch = 0;
	if(size.flags & PMaxSize) {
		c->maxw = size.max_width;
		c->maxh = size.max_height;
	}
	else
		c->maxw = c->maxh = 0;
	if(size.flags & PMinSize) {
		c->minw = size.min_width;
		c->minh = size.min_height;
	}
	else if(size.flags & PBaseSize) {
		c->minw = size.base_width;
		c->minh = size.base_height;
	}
	else
		c->minw = c->minh = 0;
	if(size.flags & PAspect) {
		c->mina = (float)size.min_aspect.y / size.min_aspect.x;
		c->maxa = (float)size.max_aspect.x / size.max_aspect.y;
	}
	else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->minw && c->maxh && c->minh
	             && c->maxw == c->minw && c->maxh == c->minh);
}

void
updatetitle(Client *c) {
	if(!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
		gettextprop(c->win, XA_WM_NAME, c->name, sizeof c->name);
	if(c->name[0] == '\0') /* hack to mark broken clients */
		strcpy(c->name, broken);
}

void
updatestatus(void) {
	if(!gettextprop(root, XA_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, "ldwm-"VERSION);
	drawbar();
}

void
updatewindowtype(Client *c) {
	Atom state = getatomprop(c, netatom[NetWMState]);
	Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

	if(state == netatom[NetWMFullscreen])
		setfullscreen(c, True);
	if(wtype == netatom[NetWMWindowTypeDialog])
		c->isfloating = True;
}

void
updatewmhints(Client *c) {
	XWMHints *wmh;

	if((wmh = XGetWMHints(dpy, c->win))) {
		if(c == mons->sel && wmh->flags & XUrgencyHint) {
			wmh->flags &= ~XUrgencyHint;
			XSetWMHints(dpy, c->win, wmh);
		}
		else
			c->isurgent = (wmh->flags & XUrgencyHint) ? True : False;
		if(wmh->flags & InputHint)
			c->neverfocus = !wmh->input;
		else
			c->neverfocus = False;
		XFree(wmh);
	}
}

void
view(const Arg *arg) {
    int i;
    unsigned int tmptag;

	if((arg->ui & TAGMASK) == mons->tagset[mons->seltags])
		return;
	mons->seltags ^= 1; /* toggle sel tagset */
	if(arg->ui & TAGMASK) {
        mons->pertag->prevtag = mons->pertag->curtag;
		mons->tagset[mons->seltags] = arg->ui & TAGMASK;
        if(arg->ui == ~0)
            mons->pertag->curtag = 0;
        else {
            for (i=0; !(arg->ui & 1 << i); i++) ;
            mons->pertag->curtag = i + 1;
        }
    } else {
        tmptag = mons->pertag->prevtag;
        mons->pertag->prevtag = mons->pertag->curtag;
        mons->pertag->curtag = tmptag;
    }
    mons->nmaster = mons->pertag->nmasters[mons->pertag->curtag];
    mons->mfact = mons->pertag->mfacts[mons->pertag->curtag];
    mons->sellt = mons->pertag->sellts[mons->pertag->curtag];
    mons->lt[mons->sellt] = mons->pertag->ltidxs[mons->pertag->curtag][mons->sellt];
    mons->lt[mons->sellt^1] = mons->pertag->ltidxs[mons->pertag->curtag][mons->sellt^1];

	focus(NULL);
	arrange();
}

Client *
wintoclient(Window w) {
	Client *c;

	for(c = mons->clients; c; c = c->next)
		if(c->win == w)
			return c;
	return NULL;
}

/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's).  Other types of errors call Xlibs
 * default error handler, which may call exit.  */
int
xerror(Display *dpy, XErrorEvent *ee) {
	if(ee->error_code == BadWindow
	|| (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch)
	|| (ee->request_code == X_PolyText8 && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable)
	|| (ee->request_code == X_PolySegment && ee->error_code == BadDrawable)
	|| (ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch)
	|| (ee->request_code == X_GrabButton && ee->error_code == BadAccess)
	|| (ee->request_code == X_GrabKey && ee->error_code == BadAccess)
	|| (ee->request_code == X_CopyArea && ee->error_code == BadDrawable))
		return 0;
	fprintf(stderr, "ldwm: fatal error: request code=%d, error code=%d\n",
			ee->request_code, ee->error_code);
	return xerrorxlib(dpy, ee); /* may call exit */
}

int
xerrordummy(Display *dpy, XErrorEvent *ee) {
	return 0;
}

/* Startup Error handler to check if another window manager
 * is already running. */
int
xerrorstart(Display *dpy, XErrorEvent *ee) {
	die("ldwm: another window manager is already running\n");
	return -1;
}

void
zoom(const Arg *arg) {
	Client *c = mons->sel;

	if(!mons->lt[mons->sellt]->arrange
	|| (mons->sel && mons->sel->isfloating))
		return;
	if(c == nexttiled(mons->clients))
		if(!c || !(c = nexttiled(c->next)))
			return;
	pop(c);
}

int
main(int argc, char *argv[]) {
	if(argc == 2 && !strcmp("-v", argv[1]))
		die("ldwm-"VERSION", © 2006-2012 ldwm engineers, see LICENSE for details\n");
	else if(argc != 1)
		die("usage: ldwm [-v]\n");
	if(!setlocale(LC_CTYPE, "") || !XSupportsLocale())
		fputs("warning: no locale support\n", stderr);
	if(!(dpy = XOpenDisplay(NULL)))
		die("ldwm: cannot open display\n");
	checkotherwm();
	setup();
	scan();
	run();
	cleanup();
	XCloseDisplay(dpy);
	return EXIT_SUCCESS;
}
