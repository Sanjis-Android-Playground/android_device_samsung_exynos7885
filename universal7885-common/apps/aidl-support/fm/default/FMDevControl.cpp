// Copyright (C) 2021 Eureka Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "FMDevControl.h"

#include <fm_slsi-impl.h>

#include <android/binder_manager.h>

#include <cassert>
#include <cerrno>
#include <chrono>

#include <fcntl.h>
#include <signal.h>

#include "CommonMacro.h"

#include <aidl/vendor/eureka/hardware/audio_route/BnAudioRoute.h>

namespace aidl::vendor::eureka::hardware::fmradio {

::ndk::ScopedAStatus FMDevControl::open(void) {
	fd = fm_radio_slsi::open_device();
	assert(fd > 0);
	fm_radio_slsi::bootctrl(fd);
	return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus FMDevControl::getValue(GetType type, int *_aidl_return) {
	if (type != GetType::GET_TYPE_FM_MUTEX_LOCKED) {
		RETURN_IF_FAILED_LOCK;
	}
	assert(fd > 0);
	switch (type) {
		case GetType::GET_TYPE_FM_FREQ:
			fm_radio_slsi::get_frequency(fd, _aidl_return);
			break;
		case GetType::GET_TYPE_FM_UPPER_LIMIT:
			*_aidl_return = fm_radio_slsi::get_upperband_limit(fd);
			break;
		case GetType::GET_TYPE_FM_LOWER_LIMIT:
			*_aidl_return = fm_radio_slsi::get_lowerband_limit(fd);
			break;
		case GetType::GET_TYPE_FM_RMSSI:
			*_aidl_return = fm_radio_slsi::get_rmssi(fd);
			break;
		case GetType::GET_TYPE_FM_BEFORE_CHANNEL:
			if (index > 0) index -= 1;
			if (kMiddleState != nullptr) {
				index = kMiddleState->first;
				delete kMiddleState;
				kMiddleState = nullptr;
			}
			fm_radio_slsi::set_frequency(fd, freqs_list[index]);
			*_aidl_return = freqs_list[index];
			break;
		case GetType::GET_TYPE_FM_NEXT_CHANNEL:
			if (index < freqs_list.size() - 1) index += 1;
			if (kMiddleState != nullptr) {
                                index = kMiddleState->second;
                                delete kMiddleState;
                                kMiddleState = nullptr;
                        }
			fm_radio_slsi::set_frequency(fd, freqs_list[index]);
			*_aidl_return = freqs_list[index];
			break;
		case GetType::GET_TYPE_FM_SYSFS_IF:
			NOT_SUPPORTED;
		case GetType::GET_TYPE_FM_MUTEX_LOCKED:
			if (lock.try_lock()){
				*_aidl_return = false;
				lock.unlock();
			} else {
				*_aidl_return = true;
			}
			break;
		default:
			break;
	};
	if (type != GetType::GET_TYPE_FM_MUTEX_LOCKED) {
		lock.unlock();
	}
	return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus FMDevControl::setValue(SetType type, int value) {
	using audio_route::IAudioRoute;

	RETURN_IF_FAILED_LOCK;
	assert(fd > 0);
	switch (type) {
		case SetType::SET_TYPE_FM_FREQ:
			fm_radio_slsi::set_frequency(fd, value);
			if (std::find(freqs_list.begin(), freqs_list.end(), value) == freqs_list.end())
				kMiddleState = saveMiddleState(value, freqs_list);
			break;
		case SetType::SET_TYPE_FM_MUTE:
			fm_radio_slsi::set_mute(fd, value);
			break;
		case SetType::SET_TYPE_FM_VOLUME:
			fm_radio_slsi::set_volume(fd, value);
			break;
		case SetType::SET_TYPE_FM_THREAD:
			fm_radio_slsi::fm_thread_set(fd, value);
			break;
		case SetType::SET_TYPE_FM_RMSSI:
			fm_radio_slsi::set_rssi(fd, value);
			break;
		case SetType::SET_TYPE_FM_SEARCH_CANCEL:
			fm_radio_slsi::stop_search(fd);
			break;
		case SetType::SET_TYPE_FM_SPEAKER_ROUTE:
			std::shared_ptr<IAudioRoute> svc = IAudioRoute::fromBinder(ndk::SpAIBinder(AServiceManager_waitForService("vendor.eureka.hardware.audio_route.IAudioRoute/default")));
			svc->setParam(value ? "routing=2": "routing=8");
			break;
		case SetType::SET_TYPE_FM_SEARCH_START:
			lock.unlock();
			search_thread = std::thread([this] {
				const std::lock_guard<std::timed_mutex> guard(lock);
				freqs_list = fm_radio_slsi::get_freqs(fd);
			});
			search_thread.detach();
			break;
		case SetType::SET_TYPE_FM_APP_PID:
			client_observe_thread = std::thread([=] {
				std::shared_ptr<IAudioRoute> svc;
				pid_t pid = value;

				while (true) {
					if (kill(pid, 0) < 0 && errno == ESRCH) break;
					std::this_thread::sleep_for(std::chrono::seconds(2));
				}

				fm_radio_slsi::fm_thread_set(fd, 0);
				svc = IAudioRoute::fromBinder(ndk::SpAIBinder(AServiceManager_waitForService("vendor.eureka.hardware.audio_route.IAudioRoute/default")));
				svc->setParam("l_fmradio_mode=off");
				close();
			}
			client_observe_thread.detach();
		default:
			break;
	};
	if (type != SetType::SET_TYPE_FM_SEARCH_START) lock.unlock();
	return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus FMDevControl::getFreqsList(std::vector<int> *_aidl_return){
	RETURN_IF_FAILED_LOCK;
	*_aidl_return = freqs_list;
	lock.unlock();
	return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus FMDevControl::close() {
	if (fd > 0) ::close(fd);
	fd = -1;
	return ::ndk::ScopedAStatus::ok();
}
} // namespace aidl::vendor::eureka::hardware::fmradio
