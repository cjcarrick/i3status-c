# i3status-c

### High efficiency i3 status command

Run `make install` to build and install a binary at /usr/local/bin/i3status-c

Edit i3 config (~/.config/i3/config) to contain something like this:

```
bar {
  status_command /usr/local/bin/i3status-c
}
```
