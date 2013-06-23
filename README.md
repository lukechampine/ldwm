ldwm - less dynamic window manager
==================================
ldwm is a less functional fork of dwm, an extremely fast, small, and dynamic window manager for X.
ldwm removes some features of dwm such as multi-monitor support, but also makes certain things easier, such as customizing status bar colors.

dwm can be found here: http://dwm.suckless.org/

Requirements
------------
In order to build ldwm you need the Xlib header files.


Installation
------------
Edit config.mk to match your local setup (ldwm is installed into the /usr/local namespace by default).

Afterwards enter the following command to build and install ldwm (as root if necessary):

    make clean install

Running ldwm
------------
Add the following line to your .xinitrc to start ldwm using startx:

    exec ldwm

In order to display status info in the bar, you can do something like this in your .xinitrc:

    while xsetroot -name "`date` `uptime | sed 's/.*,//'`"
    do
    	sleep 1
    done &
    exec ldwm

You can define colors for your status bar in config.h and reference them with \x01, \x02, etc. For example:
    
    colored () {
        echo -e "\x01This text is \x02colored!"
    }
    xsetroot -name "`colored`"

To add icons to your tags or status bar, edit your config.h to use a custom font with added icon glyphs. For example, you could change the unicode character B3 to a command prompt icon and reference it as such:

    static const char *tags[] = { "\u00B3" };

Configuration
-------------
The configuration of ldwm is done by creating a custom config.h and recompiling the source code. Otherwise, config.h will be generated automatically from config.def.h.

Summary of changes
------------------
Features removed:
* Multi-monitor support
* Multiple tag selection
* Multiple tags per client
* Support for urgency hints

Features added:
* Per-tag layouts, nmaster/mfact values, etc.
* Statusbar color support
* "tilegap" layout
* Prev/Next tag support (planned)
