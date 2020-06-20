# nv-pwr-ctrl
Simple utility to cap Nvidia GPU power limits on Linux based on max fan speed and/or max GPU temperature

## Table of Contents
* [Purpose](#purpose)
* [How to run](#how-to-run)
  * [Sample Chart](#sample-chart)
  * [Sudo Requirements](#sudo-requirements)
* [How to build](#how-to-build)
  * [Dependencies](#dependencies)
* [F.A.Q.](#faq)
* [Task list](#task-list)

## Purpose
The main reason behind this small program can be traced to the simple fact I have very loud fans of my _2080 Ti RTX_ and during summer time those spin too quickly due to high temperatures.<br/>
I could have used other utilities to control the fan speed itself, but controlling the fan speed directly is bad; if the fan is spinning it means it needs to dissipate heath, hence another way to relieve pressure from the fans spinning is to _actually_ produce less heath, i.e. **consume less power**.

Not sure if there was already such simple utility, I've decided to roll my own, to experiment with the [NVML](https://developer.nvidia.com/nvidia-management-library-nvml) libraries.

## How to run
```
Usage: ./nv-pwr-ctrl [options]
Executes nv-pwr-ctrl 0.0.2

Controls the power limit of a given Nvidia GPU based on max fan speed

-f, --max-fan f     Specifies the target max fan speed, default is 80%
    --gpu-id i      Specifies a specific gpu id to control, default is 0
    --verbose       Prints additional log every iteration (4 times a second)
    --help          Prints this help and exit

Run with root/admin privileges to be able to change the power limits

```
One can simply run the utility with `sudo ./nv-pwr-ctrl` and then push `Ctrl+C` to quit.

### Sample Chart
This chart has been produced in a ~5 minutes session of _Monster Hunter: World_. The game was playable all the time, at 3440x1440 with all graphical options/details set to max (apart _AA_) and _G-Sync_ on.<br/>I could not notice I was playing with a variable cap on _Power Limits_.

![MH:W Chart](https://raw.githubusercontent.com/Emanem/nv-pwr-ctrl/master/imgs/mhw_usage.png)

### sudo requirements
Unfortunately this utility needs `sudo` access because it invokes a function ([nvmlDeviceSetPowerManagementLimit](https://docs.nvidia.com/deploy/nvml-api/group__nvmlDeviceCommands.html#group__nvmlDeviceCommands_1gb35472a72da70c8c8e9c9b108b3640b5)) which requires such elevated privileges.

## How to build
Simply run `make clean && make release` and you're done.

### Dependencies
This executable is dependent on _NVML_ (i.e. _libnvidia-ml.so_), but it tries to load it dynamically at run time, which means that no Nvidia dependencies are required to build this.
It does require the proprietary drivers correctly installed.

## F.A.Q

1. *Can I run this on _AMD_ or _Intel_ GPUs?*<br/>No... this is for Nvidia only.
2. *Can you support open-source Nvidia drivers?*<br/>No, this is using _NVML_ propritary libary.
3. *The lower the fan speen (i.e. -f), the lower the FPS... is this expected?*<br/>Yes, because in order to keep the fan spinning at just _x_%, then the power will be limited. Having less power means keeping the GPU cooler but also less capable of coping with the demands of the CPU and the game.
4. *Can you add feature *X* please?*<br/>Open a ticket and let's discuss...
5. *Why didn't you use application *X*to achieve something similar?*<br/>I didn't want to have to install/compile other packages and didn't need _fancy_ UI. I needed a simple app I can spin with a script and stop with `Ctrl+C`. That's it.
6. *Does this work only with a specific graphics API?*<br/>No, it is in fact agnostic of any API and would also be able to _power limit_ even _compute-only_ workloads on the GPU.

## Task list

- [x] Basic functionality
- [x] `std::cout` writes CSV useful for graphs
- [ ] Rename _verbose_ option to _log_
- [ ] Drive the power limit based on GPU temperature
- [ ] ??? 
