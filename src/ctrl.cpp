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

#include "ctrl.h"
#include <string>
#include <vector>
#include <iostream>

namespace {
	class simple_fan_speed_th : public ctrl::throttle {
		const unsigned int      mfs_,
					mgt_,
					rps_;
		unsigned int		cnt_;
	public:
		simple_fan_speed_th(const ctrl::params& p) : mfs_(p.max_fan_speed), mgt_(p.max_gpu_temp), rps_(p.rep_per_second), cnt_(0) {
		}

		virtual ctrl::action check(const data& d, float& bump_factor) {
			++cnt_;
			cnt_ = cnt_%rps_;
			if(cnt_)
				return ctrl::action::PWR_CNST;

			bump_factor = 1.0*rps_;
			// ensure the temp is below the threshold
			if(d.gpu_temp >= mgt_)
				return ctrl::action::PWR_DEC;

			if(d.fan_speed > mfs_) {
				return ctrl::action::PWR_DEC;
			} else if(d.fan_speed < mfs_)
				return ctrl::action::PWR_INC;
			return ctrl::action::PWR_CNST;
		}
	};

	class wavg_fan_speed_th : public ctrl::throttle {
		const unsigned int		mfs_,
						mgt_;
		const bool			verbose_;
		std::vector<unsigned int>	wfs_;
		unsigned int			cnt_;

		double get_w_avg(void) const {
			const auto	sz = wfs_.size();
			const double	pre_weight = 0.75 / (sz-1),
			      		post_weight = 0.25;
			double		accum = 0.0;
			for(unsigned int i = 0; i < sz; ++i) {
				const unsigned int	cur_el = (i+cnt_)%sz;
				if(i < sz-1) accum += pre_weight*wfs_[cur_el];
				else accum += post_weight*wfs_[cur_el];
			}
			return 1.0*mfs_ - accum;
		}
	public:
		wavg_fan_speed_th(const ctrl::params& p) : mfs_(p.max_fan_speed), mgt_(p.max_gpu_temp), verbose_(p.verbose), cnt_(0) {
			// will decide an action every 4
			// seconds
			wfs_.resize(4*p.rep_per_second);
			for(auto& i : wfs_)
				wfs_[i] = mfs_;
		}

		virtual ctrl::action check(const data& d, float& bump_factor) {
			wfs_[cnt_++] = d.fan_speed;
			cnt_ = cnt_%(wfs_.size());
			if(cnt_)
				return ctrl::action::PWR_CNST;
			// set returned values
			bump_factor = 1.0;
			const double	avg = get_w_avg();
			if(verbose_)
				std::cerr << __FUNCTION__ << " Average: " << avg << "\ttemp: " << d.gpu_temp << std::endl;
			// ensure the temp is below the threshold
			if(d.gpu_temp >= mgt_)
				return ctrl::action::PWR_DEC;

			if(avg <= -0.5) {
				bump_factor *= -1.0*wfs_.size()*avg;
				return ctrl::action::PWR_DEC;
			} else if(avg >= 0.5) {
				bump_factor *= 0.5*wfs_.size()*avg;
				return ctrl::action::PWR_INC;
			}
			return ctrl::action::PWR_CNST;
		}
	};

	class simple_gpu_temp_th : public ctrl::throttle {
		const unsigned int	mgt_,
					rps_;
		unsigned int		cnt_;
	public:
		simple_gpu_temp_th(const ctrl::params& p) : mgt_(p.max_gpu_temp), rps_(p.rep_per_second), cnt_(0) {
		}

		virtual ctrl::action check(const data& d, float& bump_factor) {
			++cnt_;
			cnt_ = cnt_%rps_;
			if(cnt_)
				return ctrl::action::PWR_CNST;

			bump_factor = 1.0*rps_;

			if(d.gpu_temp >= mgt_) {
				return ctrl::action::PWR_DEC;
			} else if(d.gpu_temp < mgt_*0.95)
				return ctrl::action::PWR_INC;
			return ctrl::action::PWR_CNST;
		}
	};

}

ctrl::throttle* ctrl::get_fan_ctrl(const std::string& ctrl_name, const ctrl::params& p) {
	if(ctrl_name == "simple") {
		return new simple_fan_speed_th(p);
	} else if(ctrl_name == "wavg") {
		return new wavg_fan_speed_th(p);
	} else if(ctrl_name == "gpu_temp") {
		return new simple_gpu_temp_th(p);
	}

	throw std::runtime_error((std::string("Invalid fan ctrl name specified: \'") + ctrl_name + "\'").c_str());
}

