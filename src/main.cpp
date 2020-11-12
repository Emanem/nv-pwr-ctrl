/*
    This file is part of nv-pwr-ctrl.

    nv-pwr-ctrl is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    nv-pwr-ctrl is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with nv-pwr-ctrl.  If not, see <https://www.gnu.org/licenses/>.
 * */

#include <iostream>
#include <string>
#include <cstring>
#include <csignal>
#include <getopt.h>
#include <memory>
#include <dlfcn.h>
#include <time.h>
#include "ctrl.h"

namespace {
	const char*	VERSION = "0.0.5";

	// settings/options management
	namespace opt {
		unsigned int	max_fan_speed = 80, // 80% fan speed
				max_gpu_temp = 80, // 80 C temperature
				gpu_id = 0,
				sleep_interval_ms = 250;
		bool		do_not_limit = false,
				verbose = false,
				log_csv = false,
				report_max = false,
				print_current = false;
		std::string	fan_ctrl = "gpu_temp";
	}

	void print_help(const char *prog, const char *version) {
		std::cerr <<	"Usage: " << prog << " [options]\nExecutes nv-pwr-ctrl " << version << "\n\n"
				"Controls the power limit of a given Nvidia GPU based on max fan speed\n\n"
				"-f, --max-fan f     Specifies the target max fan speed, default is " << opt::max_fan_speed << "%\n"
				"-t, --max-temp t    Specifies the target max gpu temperature, default is " << opt::max_gpu_temp << "C\n"
				"    --gpu-id i      Specifies a specific gpu id to control, default is " << opt::gpu_id << "\n"
				"    --do-not-limit  Don't limit power - useful to print stats for testing\n"
				"    --fan-ctrl f    Set the fan control algorithm to 'f'. Valid values are currently:\n"
				"                    'simple'   - Reactive based on current fan speed\n"
				"                    'wavg'     - Weights averages and smooths transitions\n"
				"                    'gpu_temp' - Reactive based on GPU temperature alone\n"
				"                    Default is '" << opt::fan_ctrl << "'\n"
				"    --report-max    On exit prints how many seconds the fan speed has been\n"
				"                    above max speed\n"
				"-l, --log-csv       Prints CSV log-like information to std out\n"
				"    --verbose       Prints additional log every iteration (4 times a second)\n"
				"-c, --current       Prints current power, limit and GPU temperature on std::err\n"
				"    --help          Prints this help and exit\n\n"
				"Run with root/admin privileges to be able to change the power limits\n\n"
		<< std::flush;
	}

	int parse_args(int argc, char *argv[], const char *prog, const char *version) {
		int			c;
		static struct option	long_options[] = {
			{"max-fan",	required_argument, 0,	'f'},
			{"max-temp",	required_argument, 0,	't'},
			{"gpu-id",	required_argument, 0,	0},
			{"do-not-limit",no_argument,       0,	0},
			{"fan-ctrl",	required_argument, 0,	0},
			{"report-max",  no_argument,       0,	0},
			{"log-csv",	no_argument,       0,	'l'},
			{"verbose",	no_argument,       0,	0},
			{"help",	no_argument,	   0,	0},
			{"current",	no_argument,	   0,	0},
			{0, 0, 0, 0}
		};

		while (1) {
			// getopt_long stores the option index here
			int		option_index = 0;

			if(-1 == (c = getopt_long(argc, argv, "f:t:lc", long_options, &option_index)))
				break;

			switch (c) {
			case 0: {
				// If this option set a flag, do nothing else now
				if (long_options[option_index].flag != 0)
					break;
				if(!std::strcmp("help", long_options[option_index].name)) {
					print_help(prog, version);
					std::exit(0);
				} else if (!std::strcmp("gpu-id", long_options[option_index].name)) {
					const int	g_id = std::atoi(optarg);
					if(g_id >= 0)
						opt::gpu_id = g_id;
				} else if (!std::strcmp("verbose", long_options[option_index].name)) {
					opt::verbose = true;
				} else if (!std::strcmp("do-not-limit", long_options[option_index].name)) {
					opt::do_not_limit = true;
				} else if (!std::strcmp("fan-ctrl", long_options[option_index].name)) {
					opt::fan_ctrl = optarg;
				} else if (!std::strcmp("report-max", long_options[option_index].name)) {
					opt::report_max = true;
				} else {
					throw std::runtime_error((std::string("Unknown option: ") + long_options[option_index].name).c_str());
				}

			} break;

			case 'f': {
				const int	f_speed = std::atoi(optarg);
				if(f_speed > 0 && f_speed <= 100)
					opt::max_fan_speed = f_speed;
			} break;

			case 't': {
				const int	g_temp = std::atoi(optarg);
				if(g_temp > 0 && g_temp <= 100)
					opt::max_gpu_temp = g_temp;
			} break;

			case 'l': {
				opt::log_csv = true;
			} break;

			case 'c': {
				opt::print_current = true;
			} break;

			case '?':
			break;

			default:
				throw std::runtime_error((std::string("Invalid option '") + (char)c + "'").c_str());
			}
		}
		return optind;
	}

	bool	run = true;
	void	(*prev_sigint_handler)(int) = 0;

	void sigint_handler(int signal) {
		run = false;
		// reset to previous handler
		if(prev_sigint_handler)
			std::signal(SIGINT, prev_sigint_handler);
	}

}

namespace nvml {
	const char	SO_NAME[] = "libnvidia-ml.so";
	
	// types definitions (compatible)
	typedef void*	nvmlDevice_t;

	// function pointers definitions
	typedef int (*fp_nvmlInit_v2)(void);
	typedef int (*fp_nvmlShutdown)(void);
	typedef int (*fp_nvmlDeviceGetCount_v2)(unsigned int*);
	typedef int (*fp_nvmlDeviceGetHandleByIndex_v2)(unsigned int, nvmlDevice_t*);
	typedef int (*fp_nvmlDeviceGetName)(nvmlDevice_t, char*, unsigned int);
	typedef int (*fp_nvmlDeviceGetPowerManagementDefaultLimit)(nvmlDevice_t, unsigned int*);
	typedef int (*fp_nvmlDeviceGetPowerManagementLimit)(nvmlDevice_t, unsigned int*);
	typedef int (*fp_nvmlDeviceGetFanSpeed)(nvmlDevice_t, unsigned int*);
	typedef int (*fp_nvmlDeviceGetTemperature)(nvmlDevice_t, const int, unsigned int*);
	typedef int (*fp_nvmlDeviceGetPowerUsage)(nvmlDevice_t, unsigned int*);
	typedef int (*fp_nvmlDeviceSetPowerManagementLimit)(nvmlDevice_t, unsigned int);
	typedef const char* (*fp_nvmlErrorString)(int);

	// functions themselves
	fp_nvmlInit_v2					nvmlInit_v2 = 0;
	fp_nvmlShutdown					nvmlShutdown = 0;
	fp_nvmlDeviceGetCount_v2			nvmlDeviceGetCount_v2 = 0;
	fp_nvmlDeviceGetHandleByIndex_v2		nvmlDeviceGetHandleByIndex_v2 = 0;
	fp_nvmlDeviceGetName				nvmlDeviceGetName = 0;
	fp_nvmlDeviceGetPowerManagementDefaultLimit	nvmlDeviceGetPowerManagementDefaultLimit = 0;
	fp_nvmlDeviceGetPowerManagementLimit		nvmlDeviceGetPowerManagementLimit = 0;
	fp_nvmlDeviceGetFanSpeed			nvmlDeviceGetFanSpeed = 0;
	fp_nvmlDeviceGetTemperature			nvmlDeviceGetTemperature = 0;
	fp_nvmlDeviceGetPowerUsage			nvmlDeviceGetPowerUsage = 0;
	fp_nvmlDeviceSetPowerManagementLimit		nvmlDeviceSetPowerManagementLimit = 0;
	fp_nvmlErrorString				nvmlErrorString = 0;

	void load_functions(void* nvml_so) {
#define	LOAD_SYMBOL(x) \
	do { \
		x = (fp_##x)dlsym(nvml_so, #x); \
		if(!x) \
			throw std::runtime_error("Can't load function ##x"); \
	} while(0);

		LOAD_SYMBOL(nvmlInit_v2);
		LOAD_SYMBOL(nvmlShutdown);
		LOAD_SYMBOL(nvmlDeviceGetCount_v2);
		LOAD_SYMBOL(nvmlDeviceGetHandleByIndex_v2);
		LOAD_SYMBOL(nvmlDeviceGetName);
		LOAD_SYMBOL(nvmlDeviceGetPowerManagementDefaultLimit);
		LOAD_SYMBOL(nvmlDeviceGetPowerManagementLimit);
		LOAD_SYMBOL(nvmlDeviceGetFanSpeed);
		LOAD_SYMBOL(nvmlDeviceGetTemperature);
		LOAD_SYMBOL(nvmlDeviceGetPowerUsage);
		LOAD_SYMBOL(nvmlDeviceSetPowerManagementLimit);
		LOAD_SYMBOL(nvmlErrorString);

#undef	LOAD_SYMBOL
	}

	int pvt_nvmlDeviceGetFanSpeed(nvmlDevice_t dev, unsigned int* v) {
		const auto	rv = nvmlDeviceGetFanSpeed(dev, v);
		if(rv == 999) {
			// this is NVML_ERROR_UNKNOWN
			// and is when nvidia-settings instead
			// is able to report 125% fan speed
			// then modify the value and return
			*v = 125;
			return 0;
		}
		return rv;
	}

	// all these functions require 'load_functions'
	// to be called
	nvmlDevice_t get_device_by_id(const unsigned int id) {
		unsigned int	max_gpu = 0;
		if(const int rv = nvmlDeviceGetCount_v2(&max_gpu))
			throw std::runtime_error((std::string("nvmlDeviceGetCount_v2 failed: ") + std::to_string(rv)).c_str());
		if(opt::verbose)
			std::cerr << "Found " << max_gpu << " Nvidia GPUs" << std::endl;
		if(max_gpu < 1)
			throw std::runtime_error("Can't find any Nvidia GPU on this system");
		if(id >= max_gpu)
			throw std::runtime_error((std::string("Specified gpu id (") + std::to_string(id) + ") outside of max gpu available (" + std::to_string(max_gpu) + ")").c_str());
		nvmlDevice_t	dev;
		if(const int rv = nvmlDeviceGetHandleByIndex_v2(id, &dev))
			throw std::runtime_error((std::string("nvmlDeviceGetHandleByIndex_v2 failed: ") + std::to_string(rv)).c_str());
		return dev;
	}
}

int main(int argc, char *argv[]) {
	try {
		// setup sig handler
		std::signal(SIGINT, sigint_handler);
		// parse args and load nvml
		const auto				rv = parse_args(argc, argv, argv[0], VERSION);
		std::unique_ptr<void, void(*)(void*)>	nvml_so(dlopen(nvml::SO_NAME, RTLD_LAZY|RTLD_LOCAL), [](void* p){ if(p) dlclose(p); });
		if(!nvml_so)
			throw std::runtime_error("Can't find/load NVML");
		std::unique_ptr<ctrl::throttle>		thr(ctrl::get_fan_ctrl(opt::fan_ctrl, { opt::max_fan_speed, opt::max_gpu_temp, 1000/opt::sleep_interval_ms, opt::verbose }));
		// load nvml functions/symbols
		nvml::load_functions(nvml_so.get());

#define SAFE_NVML_CALL(x) \
	do { \
		const int rv = (x); \
		if(rv) \
			throw std::runtime_error((std::string(#x) + " failed, error (" + std::to_string(rv) + "): " + nvml::nvmlErrorString(rv)).c_str()); \
	} while(0);

		// init nvml
		SAFE_NVML_CALL(nvml::nvmlInit_v2());
		// get device by id
		const auto	dev = nvml::get_device_by_id(opt::gpu_id);
		// print out some info
		char		gpu_name[256];
		SAFE_NVML_CALL(nvml::nvmlDeviceGetName(dev, gpu_name, 256)); 
		gpu_name[255] = '\0';
		// get default power limit
		unsigned int	gpu_pwr_limit = 0;
		SAFE_NVML_CALL(nvml::nvmlDeviceGetPowerManagementDefaultLimit(dev, &gpu_pwr_limit));
		// print main info
		std::cerr << "Running on GPU[" << opt::gpu_id << "] \"" << gpu_name << "\"" << std::endl;
		std::cerr << "Current max power limit: " <<  gpu_pwr_limit << "mW, target max fan speed: " << opt::max_fan_speed << "%, max GPU temp: " << opt::max_gpu_temp << "C" << std::endl;
		std::cerr << "Fan control selected: '" << opt::fan_ctrl << "'" << std::endl;
		if(opt::do_not_limit)
			std::cerr << "Warning: '--do-not-limit' has been set, max power limit won't be modified" << std::endl;
		std::cerr << "Press Ctrl+C to quit" << std::endl;
		// main loop
		const unsigned int	PWR_DELTA = 1000,
		      			MIN_PWR_LIMIT = 50*1000; // min 50k mW
		// variable target gpu power limit
		unsigned int	tgt_gpu_pwr_limit = gpu_pwr_limit;
		size_t		iter = 0,
				fan_over_max = 0;
		if(opt::log_csv) {
			// print header
			std::cout << "Iteration,Fan Speed (%),GPU Temperature (C),Power Usage (mW),Power Limit (mW)" << std::endl;
		}
		if(opt::print_current)
			std::cerr << std::endl;
		while(run) {
			// 1. get the fan speed and temperature
			unsigned int	cur_fan_speed = 0,
					cur_gpu_temp = 0,
					cur_gpu_pwr = 0;
			SAFE_NVML_CALL(nvml::pvt_nvmlDeviceGetFanSpeed(dev, &cur_fan_speed));
			SAFE_NVML_CALL(nvml::nvmlDeviceGetTemperature(dev, 0, &cur_gpu_temp));
			SAFE_NVML_CALL(nvml::nvmlDeviceGetPowerUsage(dev, &cur_gpu_pwr));

			if(opt::log_csv) {
				std::cout << iter << "," << cur_fan_speed << "," << cur_gpu_temp << "," << cur_gpu_pwr << "," << tgt_gpu_pwr_limit << std::endl;
			}
			if(cur_fan_speed > opt::max_fan_speed)
				++fan_over_max;

			auto fn_do_sleep = [&iter](void) -> void {
				// sleep for 1/4 of a second
				struct timespec	ts = { opt::sleep_interval_ms/1000, (opt::sleep_interval_ms%1000)*1000*1000 };
				nanosleep(&ts, 0);
				++iter;
			};

			if(opt::print_current) {
				std::fprintf(stderr, "Current/Target power limit (GPU Temp): %6d/%6d (%2d)\r", cur_gpu_pwr, tgt_gpu_pwr_limit, cur_gpu_temp);
			}

			if(opt::do_not_limit) {
				fn_do_sleep();
				continue;
			}

			float	b_fact = 1.0;
			switch(thr->check({ cur_fan_speed, cur_gpu_temp }, b_fact)) {
			// 2. if the check tells us to decrease
			// then start reducing the power limit
			case ctrl::action::PWR_DEC: {
				tgt_gpu_pwr_limit -= b_fact*PWR_DELTA;
				if(tgt_gpu_pwr_limit < MIN_PWR_LIMIT) {
					tgt_gpu_pwr_limit = MIN_PWR_LIMIT;
				}
				SAFE_NVML_CALL(nvml::nvmlDeviceSetPowerManagementLimit(dev, tgt_gpu_pwr_limit));
			} break;

			case ctrl::action::PWR_INC: {
				// 3. increase the power limit
				if(tgt_gpu_pwr_limit < gpu_pwr_limit) {
					tgt_gpu_pwr_limit += b_fact*PWR_DELTA;
					if(tgt_gpu_pwr_limit > gpu_pwr_limit)
						tgt_gpu_pwr_limit = gpu_pwr_limit;
					SAFE_NVML_CALL(nvml::nvmlDeviceSetPowerManagementLimit(dev, tgt_gpu_pwr_limit));
				}
			} break;

			case ctrl::action::PWR_CNST:
			default:
				break;
			}

			fn_do_sleep();
		}
		std::cerr << "\nExiting" << std::endl;
		// before quitting, restore original power limits
		// only if those got changed
		unsigned int	cur_pwr_limit = 0;
		SAFE_NVML_CALL(nvml::nvmlDeviceGetPowerManagementLimit(dev, &cur_pwr_limit));
		if(cur_pwr_limit != gpu_pwr_limit) {
			SAFE_NVML_CALL(nvml::nvmlDeviceSetPowerManagementLimit(dev, gpu_pwr_limit));
			if(opt::verbose)
				std::cerr << "Restored original max power limit: " << gpu_pwr_limit << "mW" << std::endl;
		} else {
			if(opt::verbose)
				std::cerr << "Unchanged max power limit: " << gpu_pwr_limit << "mW" << std::endl;
		}
		// report how many seconds the fan speed was over max
		if (opt::report_max) {
			std::cerr << "Fan speed was above max (" <<opt::max_fan_speed << "%) for " << fan_over_max*opt::sleep_interval_ms/1000 << "s" << std::endl;
		}
		// shutdown nvml
		nvml::nvmlShutdown();

#undef	SAFE_NVML_CALL

	} catch(const std::exception& e) {
		std::cerr << "Exception: " << e.what() << std::endl;
		return -1;
	} catch(...) {
		std::cerr << "Unknown exception" << std::endl;
		return -2;
	}

}

