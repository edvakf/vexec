# vexec

color stdout in green and stderr in red.

## build

```
gcc vexec.c -Wall -o vexec
```

## usage

pass any shell command as arguments; eg.

```
vexec ls
```

## todo

currently, output order is not preserved. so the output of below command is not guaranteed.

```
./vexec sh -c 'echo L1; echo L2 >&2; echo L3; echo L4 >&2'
```
