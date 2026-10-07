//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  MultiStateInput.h
//
//  Helper class that distinguishes between short and long button presses.
//

#ifndef ES_CORE_MULTI_STATE_INPUT_H
#define ES_CORE_MULTI_STATE_INPUT_H

#include <string>

struct InputConfig;
struct Input;

class MultiStateInput
{
public:
    MultiStateInput(const std::string& buttonName);

    // Returns true once the button has been pressed and released again.
    bool isShortPressed(InputConfig* config, const Input& input);

    // Returns true when the button has been held down for the hold time. When this returns true
    // the pending release is consumed so that it cannot be reported as a short press.
    bool isLongPressed(int deltaTime);

private:
    std::string mButtonName;
    int mTimeHoldingButton;
    bool mIsPressed;
};

#endif // ES_CORE_MULTI_STATE_INPUT_H
