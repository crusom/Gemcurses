# Gemcurses
A Gemini client, written in C using ncurses

| KEY| ACTION        |
| -- | ------------- |
| arrows up/down | go down or up on the page      |
| /              | search                         |
| q              | change to link-mode/scroll-mode|
| enter          | go to a link                   |  
| B              | go to the defined main gemsite (antenna) |
| P              | show bookmarks dialog          |
| R              | refresh the gemsite            |
| S              | save the gemsite               |
| C              | show url of the selected link  |
| A              | bookmark current gemsite       |
| PgUp/PgDn      | go page up or page down        |
| mouse scroll   | scroll                         |

Note that some keys are *uppercase*


**Data path is $XDG\_DATA\_HOME or $HOME/.local/share/gemcurses**

**Cache path is $XDG\_CACHE\_HOME or $HOME/.cache/gemcurses** 

Look [xdgbasedirectory](https://xdgbasedirectoryspecification.com/)

![The Antenna gemsite and bookmarks dialog](/images/bookmarks.png "Example screenshot1")
![The Astrobotany gemsite](/images/astrobotany.png "Example screenshot2")


# TODO

## To add:
- [ ] ERR\_print\_errors\_fp
- [ ] basic fuzzer
- [ ] finger support
- [ ] offline support
- [x] showing <META> errors to the user
- [ ] ASCII colors maybe? (https://git.sr.ht/~kevin8t8/mutt/tree/master/item/pager.c)
- [ ] History
- [ ] Some switch if user wants to disable/enable sending certificates(?)
- [ ] Charsets  

## to fix/test:
- [ ] test input (gemini://gemini.ctrl-c.club/~stack/gemlog/2021-11-30.noncompliant.gmi)
- [ ] do my own forms implementation for better performance and resizing
- [x] gemini://spellbinding.tilde.cafe/ crashes?
- [x] use XDG dirs, not local https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
