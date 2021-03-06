/* See LICENSE file for copyright and license details. */

/* appearance */
static const char font[]            = "-*-terminus-medium-r-*-*-16-*-*-*-*-*-*-*";

static const char bordercolors[2][8] =
   /* focused    unfocused */
    { "#696969", "#212121" };

static const char tagcolors[2][ColLast][8] = {
   /* foreground background */
    { "#696969", "#121212" }, /* selected */
    { "#eeeeee", "#121212" }, /* unselected */
};

#define NUMCOLORS 4
static const char statuscolors[NUMCOLORS][ColLast][8] = {
   /* foreground background */
    { "#363636", "#121212" }, // 1 = black to gray
    { "#eeeeee", "#363636" }, // 2 = white on gray
    { "#121212", "#363636" }, // 3 = gray to black
    { "#eeeeee", "#121212" }, // 4 = white on black
};

static const unsigned int borderpx  = 1;        /* border pixel of windows */
static const unsigned int paddingpx = 10;       /* window padding in tilegap layout */
static const unsigned int snap      = 10;       /* snap pixel */
static const Bool overlap           = True;     /* False means no overlapping borders/padding */

/* tagging */
static const char *tags[] = { "1", "2", "3" };

static const Rule rules[] = {
	/* class      instance    title       tag (0 for current)  isfloating */
	{ "Firefox",  NULL,       NULL,       2,                   True },
};

/* layout(s) */
static const float mfact      = 0.55; /* factor of master area size [0.05..0.95] */
static const int nmaster      = 1;    /* number of clients in master area */
static const Bool resizehints = False; /* True means respect size hints in tiled resizals */

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "[]=",      tilegap },
	{ "><>",      floating },
	{ "[M]",      monocle },
};

/* key definitions */
#define MODKEY Mod4Mask
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.i = TAG} }, \
	{ MODKEY|ShiftMask,             KEY,      tag,            {.i = TAG} }, 

/* commands */
static const char *dmenucmd[] = { "dmenu_run", "-fn", font, "-nb", statuscolors[0][ColBG], "-nf", statuscolors[0][ColFG], "-sb", statuscolors[1][ColBG], "-sf", statuscolors[1][ColFG], NULL };
static const char *termcmd[] = { "urxvt", NULL };

static Key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY,                       XK_r,      spawn,          {.v = dmenucmd } },
	{ MODKEY,                       XK_Return, spawn,          {.v = termcmd } },
	{ MODKEY,                       XK_j,      focusstack,     {.i = +1} },
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1} },
	{ MODKEY,                       XK_i,      incnmaster,     {.i = +1} },
	{ MODKEY,                       XK_d,      incnmaster,     {.i = -1} },
	{ MODKEY,                       XK_h,      setmfact,       {.f = -0.05} },
	{ MODKEY,                       XK_l,      setmfact,       {.f = +0.05} },
    { MODKEY|ShiftMask,             XK_Return, zoom,           {0} },
	{ MODKEY,                       XK_Tab,    view,           {.i = -1} },
	{ MODKEY|ShiftMask,             XK_c,      killclient,     {0} },
	{ MODKEY,                       XK_t,      setlayout,      {.i = 0} },
	{ MODKEY|ShiftMask,             XK_t,      setlayout,      {.i = 1} },
	{ MODKEY,                       XK_f,      setlayout,      {.i = 2} },
	{ MODKEY,                       XK_m,      setlayout,      {.i = 3} },
	{ MODKEY,                       XK_space,  setlayout,      {.i =-1} },
	{ MODKEY|ShiftMask,             XK_space,  togglefloating, {0} },
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	{ MODKEY|ShiftMask,             XK_q,      quit,           {0} },
};

/* button definitions */
/* click can be ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
static Button buttons[] = {
	/* click                event mask      button          function        argument */
	{ ClkLtSymbol,          0,              Button1,        setlayout,      {.i = -1} },
	{ ClkWinTitle,          0,              Button2,        zoom,           {0} },
	{ ClkStatusText,        0,              Button2,        spawn,          {.v = termcmd} },
	{ ClkClientWin,         MODKEY,         Button1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         Button2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         Button3,        resizemouse,    {0} },
	{ ClkTagBar,            0,              Button1,        view,           {0} },
};

