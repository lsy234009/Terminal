/*++
Copyright (c) Microsoft Corporation

Module Name:
- IInteractivityFactory.hpp

Abstract:
- Defines methods for a factory class that picks the implementation of
  interfaces depending on whether the console is running on OneCore or a larger
  edition of Windows with all the requisite API's to run the full console.

Author(s):
- Hernan Gatta (HeGatta) 29-Mar-2017
--*/

#pragma once

#include "IConsoleControl.hpp"
#include "IConsoleInputThread.hpp"

#include "IHighDpiApi.hpp"
#include "IWindowMetrics.hpp"
#include "IAccessibilityNotifier.hpp"
#include "ISystemConfigurationProvider.hpp"
#include "IInputServices.hpp"

namespace Microsoft
{
    namespace Console
    {
        namespace Interactivity
        {
            class IInteractivityFactory
            {
            public:
                virtual NTSTATUS CreateConsoleControl(IConsoleControl** control) = 0;
                virtual NTSTATUS CreateConsoleInputThread(IConsoleInputThread** thread) = 0;

                virtual NTSTATUS CreateHighDpiApi(IHighDpiApi** api) = 0;
                virtual NTSTATUS CreateWindowMetrics(IWindowMetrics** metrics) = 0;
                virtual NTSTATUS CreateAccessibilityNotifier(IAccessibilityNotifier** notifier) = 0;
                virtual NTSTATUS CreateSystemConfigurationProvider(ISystemConfigurationProvider** provider) = 0;
                virtual NTSTATUS CreateInputServices(IInputServices** services) = 0;
            };
        };
    };
};
