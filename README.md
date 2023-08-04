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

## Performance of Multithreaded Branch

Hyperfine results on Intel 8600k (6 cores, 5GHz)

### Single threaded (from main branch)
  Time (mean ± σ):      52.1 ms ±  19.2 ms    [User: 19.2 ms, System: 21.7 ms]
  Range (min … max):    24.1 ms …  90.1 ms    96 runs

### Multithreaded
  Time (mean ± σ):      29.1 ms ±  10.6 ms    [User: 10.7 ms, System: 14.1 ms]
  Range (min … max):    11.4 ms …  50.8 ms    119 runs

