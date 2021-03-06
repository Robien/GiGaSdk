/*
 * Copyright 2016 Gigatribe
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CurlProgress.h"
#include "../../rest/HttpErrors.h"

#include <curl_easy.h>
#include <mutex>
#include <curl_exception.h>
#include <chrono>
#include <thread>

using namespace std::chrono;

namespace giga
{
namespace details
{

CurlProgress::CurlProgress (pplx::cancellation_token token) :
        _mut{}, _item{0ul, 0ul, 0ul, 0ul}, _cancelToken{token}, _pause{false}, _isPaused{false}, _curl{nullptr},
        _limitRate{0ul}, _currentLimitRate{0ul}, _rateTime{}, _rateBytes{0ul}, _bucket{0ul}, _upPostion{0ul}
{
}

CurlProgress::CurlProgress (const CurlProgress& other) :
        _mut{},
        _item{other._item.dltotal, other._item.dlnow, other._item.ultotal, other._item.ulnow},
        _cancelToken{other._cancelToken},
        _pause{other._pause},
        _isPaused{other._isPaused},
        _curl{other._curl},
        _limitRate{other._limitRate},
        _currentLimitRate{other._currentLimitRate},
        _rateTime{other._rateTime},
        _rateBytes{other._rateBytes},
        _bucket{other._bucket},
        _upPostion{other._upPostion}
{
}

CurlProgress::Item
CurlProgress::data () const
{
    std::lock_guard<std::mutex> l(_mut);
    return _item;
}

void
CurlProgress::setPause (bool pause)
{
    std::lock_guard<std::mutex> l(_mut);
    _pause = pause;
}

void
CurlProgress::setUploadPosition (uint64_t pos)
{
    std::lock_guard<std::mutex> l(_mut);
    _upPostion = pos;
}

void
CurlProgress::setLimitRate (uint64_t rate)
{
    std::lock_guard<std::mutex> l(_mut);
    _limitRate = rate;
}

void
CurlProgress::setCurl (curl::curl_easy& curl)
{
    std::lock_guard<std::mutex> l(_mut);
    _curl = &curl;
}

bool
CurlProgress::isPaused () const
{
    std::lock_guard<std::mutex> l(_mut);
    return _pause;
}

int
CurlProgress::onCallback (curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) noexcept
{
    try {
        uint64_t limitRate = 0;
        {
            std::lock_guard<std::mutex> l(_mut);
            limitRate = _limitRate;

            _item.dltotal = static_cast<uint64_t>(dltotal);
            _item.dlnow   = static_cast<uint64_t>(dlnow);
            _item.ultotal = static_cast<uint64_t>(ultotal + _upPostion);
            _item.ulnow   = static_cast<uint64_t>(ulnow + _upPostion);

            if (_pause != _isPaused && _curl != nullptr)
            {
                _curl->pause(_pause ? CURLPAUSE_ALL : CURLPAUSE_CONT);
                _isPaused = _pause;
            }
            if (_cancelToken.is_canceled())
            {
                return CURLE_ABORTED_BY_CALLBACK;
            }
        }

        // Do the limit rate outside of the mutex locked zone
        // because there is waiting here.
        auto transfered = static_cast<uint64_t>(dlnow + ulnow);
        if (limitRate != _currentLimitRate || transfered == 0ul)
        {
            _rateBytes = transfered;
            _currentLimitRate = limitRate;
            _rateTime = high_resolution_clock::now();
            _bucket = _limitRate; // give it 1 sec
        }
        if (limitRate > 0 && transfered > 0)
        {
            auto now = high_resolution_clock::now();
            do
            {
                // calculate duration
                duration<float> duration = now - _rateTime;
                auto elapsedMs = duration_cast<milliseconds>(duration).count();
                _rateTime = now;

                // add bytes in the bucket
                _bucket += std::max(static_cast<uint64_t>((elapsedMs * limitRate) / 1000), static_cast<uint64_t>(1ul));

                // take bytes in the bucket
                auto take = std::min(_bucket, transfered - _rateBytes);
                _bucket    -= take;
                _rateBytes += take;

                // wait for new bytes.
                if (_bucket == 0)
                {
                    std::this_thread::sleep_for(milliseconds(500));
                    now = high_resolution_clock::now();
                }
            } while (_bucket == 0 && (dlnow - _rateBytes) > 0);
        }
    } catch (...) {
        return CURLE_OBSOLETE40;
    }
    return CURLE_OK;
}

} /* namespace details */
} /* namespace giga */
