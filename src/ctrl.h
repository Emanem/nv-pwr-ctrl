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

#ifndef _CTRL_H_
#define _CTRL_H_

#include <string>

namespace ctrl {
	enum action {
		PWR_INC = 0,
		PWR_DEC,
		PWR_CNST
	};

	class throttle {
	public:
		struct data {
			unsigned int	fan_speed,
					gpu_temp;
		};

		virtual action check(const data& d, float& bump_factor) = 0;

		virtual ~throttle() {
		}
	};

	struct params {
		unsigned int	max_fan_speed,
				rep_per_second;
		bool		verbose;
	};

	extern throttle* get_fan_ctrl(const std::string& ctrl_name, const params& p);
}

#endif //_CTRL_H_

