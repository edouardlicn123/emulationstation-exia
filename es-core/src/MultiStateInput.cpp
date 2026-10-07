//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  MultiStateInput.cpp
//
//  Helper class that distinguishes between short and long button presses.
//

#include "MultiStateInput.h"

#include "InputConfig.h"

// The time in milliseconds that a button must be held down to be reported as a long press.
constexpr int HOLD_TIME {500};

MultiStateInput::MultiStateInput(const std::string& buttonName)
    : mButtonName {buttonName}
    , mTimeHoldingButton {0}
    , mIsPressed {false}
{
}

bool MultiStateInput::isShortPressed(InputConfig* config, const Input& input)
{
    if (config == nullptr || !config->isMappedTo(mButtonName, input)) {
        mIsPressed = false;
        return false;
    }

    const bool pressed {input.value != 0};

    if (pressed == mIsPressed)
        return false;

    mIsPressed = pressed;

    // Only report a short press when the button is released again.
    if (!mIsPressed)
        return true;

    mTimeHoldingButton = 0;

    return false;
}

bool MultiStateInput::isLongPressed(int deltaTime)
{
    if (!mIsPressed)
        return false;

    mTimeHoldingButton += deltaTime;

    if (mTimeHoldingButton >= HOLD_TIME) {
        // Consume the pending release so that releasing the button is not reported as a
        // short press, which would otherwise launch the game.
        mIsPressed = false;
        return true;
    }

    return false;
}
