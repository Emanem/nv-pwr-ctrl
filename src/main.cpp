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
#include <fstream>
#include <cstring>
#include <csignal>
#include <getopt.h>
#include <memory>
#include <dlfcn.h>
#include <time.h>

namespace {
	const char*	VERSION = "0.0.3";

	// settings/options management
	namespace opt {
		unsigned int	max_fan_speed = 80,
				gpu_id = 0;
		bool		do_not_limit = false,
				verbose = false;
		std::string	logfile;
	}

	void print_help(const char *prog, const char *version) {
		std::cerr <<	"Usage: " << prog << " [options]\nExecutes nv-pwr-ctrl " << version << "\n\n"
				"Controls the power limit of a given Nvidia GPU based on max fan speed\n\n"
				"-f, --max-fan f     Specifies the target max fan speed, default is " << opt::max_fan_speed << "%\n"
				"    --gpu-id i      Specifies a specific gpu id to control, default is " << opt::gpu_id << "\n"
				"    --do-not-limit  Don't limit power - useful to print stats for testing\n"
				"-l, --log-csv l     Prints CSV log-like information to file l\n"
				"    --verbose       Prints additional log every iteration (4 times a second)\n"
				"    --help          Prints this help and exit\n\n"
				"Run with root/admin privileges to be able to change the power limits\n\n"
		<< std::flush;
	}

	int parse_args(int argc, char *argv[], const char *prog, const char *version) {
		int			c;
		static struct option	long_options[] = {
			{"max-fan",	required_argument, 0,	'f'},
			{"gpu-id",	required_argument, 0,	0},
			{"do-not-limit",no_argument,       0,	0},
			{"log-csv",	required_argument, 0,	'l'},
			{"verbose",	no_argument,       0,	0},
			{"help",	no_argument,	   0,	0},
			{0, 0, 0, 0}
		};

		while (1) {
			// getopt_long stores the option index here
			int		option_index = 0;

			if(-1 == (c = getopt_long(argc, argv, "f:l:", long_options, &option_index)))
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
				} else {
					throw std::runtime_error((std::string("Unknown option: ") + long_options[option_index].name).c_str());
				}

			} break;

			case 'f': {
				const int	f_speed = std::atoi(optarg);
				if(f_speed > 0 && f_speed <= 100)
					opt::max_fan_speed = f_speed;
			} break;

			case 'l': {
				opt::logfile = optarg;
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

#undef	LOAD_SYMBOL
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
		if(id > max_gpu)
			throw std::runtime_error((std::string("Specified gpu id (") + std::to_string(id) + ") outside of max gpu available (" + std::to_string(max_gpu) + ")").c_str());
		nvmlDevice_t	dev;
		if(const int rv = nvmlDeviceGetHandleByIndex_v2(id, &dev))
			throw std::runtime_error((std::string("nvmlDeviceGetHandleByIndex_v2 failed: ") + std::to_string(rv)).c_str());
		return dev;
	}

	class throttle {
	public:
		enum action {
			PWR_INC = 0,
			PWR_DEC,
			PWR_CNST
		};

		struct data {
			unsigned int	fan_speed,
					gpu_temp;
		};

		virtual action check(const data& d) = 0;

		virtual ~throttle() {
		}
	};

	class fan_speed_th : public throttle {
		const unsigned int	mfs_;
	public:
		fan_speed_th(const unsigned int mfs) : mfs_(mfs) {
		}

		virtual action check(const data& d) {
			if(d.fan_speed > mfs_)
				return action::PWR_DEC;
			else if(d.fan_speed < mfs_)
				return action::PWR_INC;
			return action::PWR_CNST;
		}
	};
}

int main(int argc, char *argv[]) {
	try {
		// setup sig handler
		std::signal(SIGINT, sigint_handler);
		// parse args and load nvml
		const auto				rv = parse_args(argc, argv, argv[0], VERSION);
		std::unique_ptr<std::ofstream>		log_csv(opt::logfile.empty() ? 0 : new std::ofstream(opt::logfile));
		if(!opt::logfile.empty() && !log_csv)
			throw std::runtime_error(std::string("Can't open log csv file \"" + opt::logfile + "\"").c_str());
		std::unique_ptr<void, void(*)(void*)>	nvml_so(dlopen(nvml::SO_NAME, RTLD_LAZY|RTLD_LOCAL), [](void* p){ if(p) dlclose(p); });
		if(!nvml_so)
			throw std::runtime_error("Can't find/load NVML");
		std::unique_ptr<nvml::throttle>		thr(new nvml::fan_speed_th(opt::max_fan_speed));
		// load nvml functions/symbols
		nvml::load_functions(nvml_so.get());

#define SAFE_NVML_CALL(x) \
	do { \
		const int rv = (x); \
		if(rv) \
			throw std::runtime_error((std::string(#x) + " failed, error: " + std::to_string(rv)).c_str()); \
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
		std::cerr << "Current max power limit: " <<  gpu_pwr_limit << "mW, target max fan speed: " << opt::max_fan_speed << "%" << std::endl;
		std::cerr << "Press Ctrl+C to quit" << std::endl;
		// main loop
		const unsigned int	PWR_DELTA = 1000,
		      			MIN_PWR_LIMIT = 50*1000; // min 50k mW
		// variable target gpu power limit
		unsigned int	tgt_gpu_pwr_limit = gpu_pwr_limit;
		size_t		iter = 0;
		if(log_csv) {
			// print header
			*log_csv << "Iteration,Fan Speed (%),GPU Temperature (C),Power Usage (mW),Power Limit (mW)" << std::endl;
		}
		while(run) {
			// 1. get the fan speed and temperature
			unsigned int	cur_fan_speed = 0,
					cur_gpu_temp = 0,
					cur_gpu_pwr = 0;
			SAFE_NVML_CALL(nvml::nvmlDeviceGetFanSpeed(dev, &cur_fan_speed));
			SAFE_NVML_CALL(nvml::nvmlDeviceGetTemperature(dev, 0, &cur_gpu_temp));
			SAFE_NVML_CALL(nvml::nvmlDeviceGetPowerUsage(dev, &cur_gpu_pwr));

			if(log_csv) {
				*log_csv << iter << "," << cur_fan_speed << "," << cur_gpu_temp << "," << cur_gpu_pwr << "," << tgt_gpu_pwr_limit << std::endl;
			}

			auto fn_do_sleep = [&iter](void) -> void {
				// sleep for 1/4 of a second
				struct timespec	ts = { 0, 250*1000*1000 };
				nanosleep(&ts, 0);
				++iter;
			};

			if(opt::do_not_limit) {
				fn_do_sleep();
				continue;
			}

			switch(thr->check({ cur_fan_speed, cur_gpu_temp })) {
			// 2. if the check tells us to decrease
			// then start reducing the power limit
			case nvml::throttle::action::PWR_DEC: {
				tgt_gpu_pwr_limit -= PWR_DELTA;
				if(tgt_gpu_pwr_limit < MIN_PWR_LIMIT) {
					tgt_gpu_pwr_limit = MIN_PWR_LIMIT;
				}
				SAFE_NVML_CALL(nvml::nvmlDeviceSetPowerManagementLimit(dev, tgt_gpu_pwr_limit));
			} break;

			case nvml::throttle::action::PWR_INC: {
				// 3. increase the power limit
				if(tgt_gpu_pwr_limit < gpu_pwr_limit) {
					tgt_gpu_pwr_limit += PWR_DELTA;
					if(tgt_gpu_pwr_limit > gpu_pwr_limit)
						tgt_gpu_pwr_limit = gpu_pwr_limit;
					SAFE_NVML_CALL(nvml::nvmlDeviceSetPowerManagementLimit(dev, tgt_gpu_pwr_limit));
				}
			} break;

			case nvml::throttle::action::PWR_CNST:
			default:
				break;
			}

			fn_do_sleep();
		}
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

