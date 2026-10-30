//  SPDX-License-Identifier: MIT
//
//  ES-DE Frontend
//  GuiSystemSortOptions.cpp
//
//  Platform sorting menu, opened with the select button from the system view.
//

#include "guis/GuiSystemSortOptions.h"

#include "Settings.h"
#include "SystemData.h"
#include "components/OptionListComponent.h"
#include "utils/LocalizationUtil.h"
#include "views/ViewController.h"

GuiSystemSortOptions::GuiSystemSortOptions()
    : GuiSettings {_("PLATFORM SORTING")}
{
    const std::string selectedSorting {Settings::getInstance()->getString("SystemsSorting")};

    auto sorting {std::make_shared<OptionListComponent<std::string>>(_("SORT PLATFORMS BY"),
                                                                     false)};
    sorting->add(_("LOCALIZED NAME"), "name_localized", selectedSorting == "name_localized");
    sorting->add(_("PLATFORM TYPE, THEN NAME"), "platform_type",
                 selectedSorting == "platform_type");
    sorting->add(_("MANUFACTURER, THEN NAME"), "manufacturer_name",
                 selectedSorting == "manufacturer_name");
    addWithLabel(_("SORT PLATFORMS BY"), sorting);

    addSaveFunc([this, sorting] {
        // A legacy sorting value may be active, in which case no entry is selected and the
        // current setting is left untouched.
        if (sorting->getSelectedObjects().size() == 0)
            return;

        if (sorting->getSelected() == Settings::getInstance()->getString("SystemsSorting"))
            return;

        Settings::getInstance()->setString("SystemsSorting", sorting->getSelected());
        setNeedsSaving();
        SystemData::sortSystems();
        ViewController::getInstance()->sortSystemsAndRefresh();
    });
}
