# i3status-c

### High efficiency i3 status command

### Sample output:

```
NET: 10.0.0.163 | CPU: 0.00% 27.8° 4.0G │ GPU: 5.00% 54° 696M │ VRAM: 1.20G │ MEM: 2.93G │ SWAP: 1.02K │ /: 40.1G │ Monday, Jul 31 14:40:13
```

The time updates every second. Other things update every 4 seconds.

## Usage

Run `make install` to build and install a binary at /usr/local/bin/i3status-c

Edit i3 config (~/.config/i3/config) to contain something like this:

```
bar {
  status_command /usr/local/bin/i3status-c
}
```
