#include "settingsmanager.h"
#include "qvnamespace.h"

#include <QSettings>
#include <QTranslator>
#include <QLocale>
#include <QCoreApplication>
#include <QDir>
#include <QColor>

#include <QDebug>

SettingsManager::SettingsManager(QObject *parent) : QObject(parent)
{
    initializeSettingsLibrary();
    loadSettings();
    loadTranslations();
}

QString SettingsManager::getSystemLanguage() const
{
    const auto languages = QLocale::system().uiLanguages();
    for (auto language : languages)
    {
        language.replace('-', '_');
        const QString lowerLanguage = language.toLower();
        if (lowerLanguage.startsWith(QStringLiteral("zh_tw"))
            || lowerLanguage.startsWith(QStringLiteral("zh_hk"))
            || lowerLanguage.startsWith(QStringLiteral("zh_hant")))
            return QStringLiteral("zh_Hant");
        if (lowerLanguage.startsWith(QStringLiteral("zh")))
            return QStringLiteral("zh_Hans");
        if (lowerLanguage.startsWith(QStringLiteral("es")))
            return QStringLiteral("es");
        if (lowerLanguage.startsWith(QStringLiteral("ja")))
            return QStringLiteral("ja");
        if (lowerLanguage.startsWith(QStringLiteral("en")))
            return QStringLiteral("en");
    }
    return QStringLiteral("en");
}

void SettingsManager::loadTranslations()
{
    QString language = getString("language");
    if (language == "system")
        language = getSystemLanguage();

    if (language == "en")
        return;

    if (qtTranslator.load(QLocale(language), "qtbase", "_", ":/qt-translations"))
        QCoreApplication::installTranslator(&qtTranslator);

    if (appTranslator.load("qview_" + language + ".qm", ":/i18n"))
        QCoreApplication::installTranslator(&appTranslator);
}

void SettingsManager::loadSettings()
{
    QSettings settings;
    settings.beginGroup("options");
    bool changed = false;

    const auto keys = settingsLibrary.keys();
    for (const auto &key : keys)
    {
        auto &setting = settingsLibrary[key];
        if (setting.value != settings.value(key, setting.defaultValue))
            changed = true;

        setting.value = settings.value(key, setting.defaultValue);
    }

    if (changed)
        emit settingsUpdated();
}

const QVariant SettingsManager::getSetting(const QString &key, bool defaults) const
{
    auto value = settingsLibrary.value(key);

    if (!defaults && !value.value.isNull())
        return value.value;

    if (!value.defaultValue.isNull())
        return value.defaultValue;

    qWarning() << "Error: Invalid settings key: " + key;
    return QVariant();
}

bool SettingsManager::getBoolean(const QString &key, bool defaults) const
{
    auto value = getSetting(key, defaults);

    if (value.canConvert<bool>())
        return value.value<bool>();

    qWarning() << "Error: Can't convert setting key " + key + " to bool";
    return false;
}

int SettingsManager::getInteger(const QString &key, bool defaults) const
{
    auto value = getSetting(key, defaults);

    if (value.canConvert<int>())
        return value.value<int>();

    qWarning() << "Error: Can't convert setting key " + key + " to int";
    return 0;
}

double SettingsManager::getDouble(const QString &key, bool defaults) const
{
    auto value = getSetting(key, defaults);

    if (value.canConvert<double>())
        return value.value<double>();

    qWarning() << "Error: Can't convert setting key " + key + " to double";
    return 0;
}

const QString SettingsManager::getString(const QString &key, bool defaults) const
{
    auto value = getSetting(key, defaults);

    if (value.canConvert<QString>())
        return value.value<QString>();

    qWarning() << "Error: Can't convert setting key " + key + " to string";
    return "";
}

bool SettingsManager::isDefault(const QString &key) const
{
    return getSetting(key) == getSetting(key, true);
}

void SettingsManager::migrateOldSettings()
{
    if (!QSettings().contains("firstlaunch"))
        copyFromOfficial();

    QSettings settings;
    settings.beginGroup("options");

    if (!settings.contains("smoothscalingmode") && settings.contains("filteringenabled"))
    {
        const auto value =
            settings.value("scalingenabled").toBool() ? Qv::SmoothScalingMode::Expensive :
            settings.value("filteringenabled").toBool() ? Qv::SmoothScalingMode::Bilinear :
            Qv::SmoothScalingMode::Disabled;
        settings.setValue("smoothscalingmode", static_cast<int>(value));
    }

    // Replace the removed independent background/titlebar switches with one
    // deterministic theme. Existing users who explicitly chose the old dark
    // titlebar or the former default dark background keep the dark appearance;
    // new installations use the light theme by default.
    if (!settings.contains("theme") &&
        (settings.contains("titlebaralwaysdark") || settings.contains("bgcolorenabled") || settings.contains("bgcolor")))
    {
        const bool oldDarkTitlebar = settings.value("titlebaralwaysdark", false).toBool();
        const bool oldDarkBackground = settings.value("bgcolorenabled", false).toBool() &&
            QColor(settings.value("bgcolor").toString()).name().compare(QStringLiteral("#212121"), Qt::CaseInsensitive) == 0;
        settings.setValue("theme", static_cast<int>(oldDarkTitlebar || oldDarkBackground ? Qv::Theme::Dark : Qv::Theme::Light));
    }

    // The old startup-notification switch was replaced by an explicit
    // frequency.  Keep the new installation default deterministic and remove
    // the obsolete format-disable store now that the Formats pane is gone.
    if (!settings.contains("updatecheckfrequency"))
        settings.setValue("updatecheckfrequency", static_cast<int>(Qv::UpdateCheckFrequency::Weekly));
    const QString language = settings.value("language", QStringLiteral("en")).toString();
    if (language != QStringLiteral("system")
        && language != QStringLiteral("en")
        && language != QStringLiteral("zh_Hans")
        && language != QStringLiteral("zh_Hant")
        && language != QStringLiteral("es")
        && language != QStringLiteral("ja"))
        settings.setValue("language", QStringLiteral("en"));
    settings.remove("updatenotifications");
    settings.remove("disabledfileextensions");
    settings.remove("slideshowkeepswindowontop");

    // Removed Preferences controls are now fixed policies. Reset values from
    // older installations so an obsolete, previously customized checkbox
    // cannot silently revive after the control disappears.
    const QHash<QString, QVariant> removedPreferenceDefaults {
        { "windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never) },
        { "aftermatchingsizemode", static_cast<int>(Qv::AfterMatchingSize::CenterOnPrevious) },
        { "minwindowresizedpercentage", 20 },
        { "maxwindowresizedpercentage", 70 },
        { "fullscreendetails", false },
        { "mainmenuicons", false },
        { "contextmenuicons", true },
        { "submenuicons", true },
        { "persistsession", false },
        { "allowmimecontentdetection", true },
        { "skiphidden", true },
        { "saverecents", true },
        { "scalingtwoenabled", true },
        { "smoothscalinglimitenabled", false },
        { "smoothscalinglimitpercent", 400 },
        { "scalefactor", 25 },
        { "cursorzoom", true },
        { "onetoonepixelsizing", false },
        { "calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit) },
        { "fitzoomlimitenabled", false },
        { "fitzoomlimitpercent", 100 },
        { "fitoverscan", 0 },
        { "navresetszoom", true },
        { "constrainimageposition", true },
        { "constraincentersmallimage", true },
        { "originalsizeastoggle", false },
        { "colorspaceconversion", static_cast<int>(Qv::ColorSpaceConversion::AutoDetect) },
        { "navspeed", 50 },
        { "loopfoldersenabled", false },
    };
    for (auto it = removedPreferenceDefaults.cbegin(); it != removedPreferenceDefaults.cend(); ++it)
        settings.setValue(it.key(), it.value());
}

void SettingsManager::copyFromOfficial()
{
    // Migrate settings from the legacy qView organization once; runtime identity is Fovelle.
    const QSet<QString> keysToSkip = []()
    {
        QList<QString> systemDefaultKeys = QSettings{"qView", "NonExistent"}.allKeys();
        return QSet<QString>{systemDefaultKeys.begin(), systemDefaultKeys.end()};
    }();
    QSettings src{"qView", "qView"};
    QSettings dst{};

    for (const QString &key : src.allKeys())
    {
        if (keysToSkip.contains(key)) continue;
        dst.setValue(key, src.value(key));
    }
}

void SettingsManager::initializeSettingsLibrary()
{
    // Window
    settingsLibrary.insert("theme", {static_cast<int>(Qv::Theme::Light), {}});
    settingsLibrary.insert("checkerboardbackground", {false, {}});
    settingsLibrary.insert("titlebarmode", {static_cast<int>(Qv::TitleBarText::Practical), {}});
    settingsLibrary.insert("customtitlebartext", {"%z - %n", {}});
    settingsLibrary.insert("windowresizemode", {static_cast<int>(Qv::WindowResizeMode::WhenLaunching), {}});
    settingsLibrary.insert("aftermatchingsizemode", {static_cast<int>(Qv::AfterMatchingSize::CenterOnPrevious), {}});
    settingsLibrary.insert("minwindowresizedpercentage", {20, {}});
    settingsLibrary.insert("maxwindowresizedpercentage", {70, {}});
    settingsLibrary.insert("menubarenabled", {false, {}});
    settingsLibrary.insert("fullscreendetails", {false, {}});
    settingsLibrary.insert("mainmenuicons", {false, {}});
    settingsLibrary.insert("contextmenuicons", {true, {}});
    settingsLibrary.insert("submenuicons", {true, {}});
    // Compatibility value retained as a fixed false policy after the
    // Preferences control was removed.
    settingsLibrary.insert("slideshowkeepswindowontop", {false, {}});
    settingsLibrary.insert("reusewindow", {false, {}});
    settingsLibrary.insert("persistsession", {false, {}});
    // Image
    settingsLibrary.insert("smoothscalingmode", {static_cast<int>(Qv::SmoothScalingMode::Bilinear), {}});
    settingsLibrary.insert("scalingtwoenabled", {true, {}});
    settingsLibrary.insert("smoothscalinglimitenabled", {false, {}});
    settingsLibrary.insert("smoothscalinglimitpercent", {400, {}});
    settingsLibrary.insert("scalefactor", {25, {}});
    settingsLibrary.insert("cursorzoom", {true, {}});
    settingsLibrary.insert("navresetszoom", {true, {}});
    // Usually not desired due to the way macOS does DPI scaling
    settingsLibrary.insert("onetoonepixelsizing", {false, {}});
    settingsLibrary.insert("smallimageoneone", {false, {}});
    settingsLibrary.insert("calculatedzoommode", {static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit), {}});
    settingsLibrary.insert("fitzoomlimitenabled", {false, {}});
    settingsLibrary.insert("fitzoomlimitpercent", {100, {}});
    settingsLibrary.insert("fitoverscan", {0, {}});
    settingsLibrary.insert("constrainimageposition", {true, {}});
    settingsLibrary.insert("constraincentersmallimage", {true, {}});
    settingsLibrary.insert("disabledelayedconstraint", {false, {}});
    settingsLibrary.insert("originalsizeastoggle", {false, {}});
    settingsLibrary.insert("colorspaceconversion", {static_cast<int>(Qv::ColorSpaceConversion::AutoDetect), {}});
    // Miscellaneous
    settingsLibrary.insert("language", {"en", {}});
    settingsLibrary.insert("sortmode", {static_cast<int>(Qv::SortMode::Name), {}});
    settingsLibrary.insert("sortdescending", {false, {}});
    settingsLibrary.insert("preloadingmode", {static_cast<int>(Qv::PreloadMode::Adjacent), {}});
    settingsLibrary.insert("navspeed", {50, {}});
    settingsLibrary.insert("loopfoldersenabled", {false, {}});
    settingsLibrary.insert("slideshowdirection", {static_cast<int>(Qv::SlideshowDirection::Forward), {}});
    settingsLibrary.insert("slideshowtimer", {5, {}});
    settingsLibrary.insert("afterdelete", {static_cast<int>(Qv::AfterDelete::MoveForward), {}});
    settingsLibrary.insert("askdelete", {true, {}});
    settingsLibrary.insert("allowmimecontentdetection", {true, {}});
    settingsLibrary.insert("skiphidden", {true, {}});
    settingsLibrary.insert("saverecents", {true, {}});
    settingsLibrary.insert("updatecheckfrequency", {static_cast<int>(Qv::UpdateCheckFrequency::Weekly), {}});
    // Mouse
    settingsLibrary.insert("navigationregionsenabled", {false, {}});
    settingsLibrary.insert("viewportdoubleclickaction", {static_cast<int>(Qv::ViewportClickAction::ToggleFullScreen), {}});
    settingsLibrary.insert("viewportaltdoubleclickaction", {static_cast<int>(Qv::ViewportClickAction::ToggleTitlebarHidden), {}});
    settingsLibrary.insert("viewportdragaction", {static_cast<int>(Qv::ViewportDragAction::Pan), {}});
    settingsLibrary.insert("viewportaltdragaction", {static_cast<int>(Qv::ViewportDragAction::MoveWindow), {}});
    settingsLibrary.insert("viewportmiddlebuttonmode", {static_cast<int>(Qv::ClickOrDrag::Click), {}});
    settingsLibrary.insert("viewportmiddleclickaction", {static_cast<int>(Qv::ViewportClickAction::ZoomToFit), {}});
    settingsLibrary.insert("viewportaltmiddleclickaction", {static_cast<int>(Qv::ViewportClickAction::OriginalSize), {}});
    settingsLibrary.insert("viewportmiddledragaction", {static_cast<int>(Qv::ViewportDragAction::Pan), {}});
    settingsLibrary.insert("viewportaltmiddledragaction", {static_cast<int>(Qv::ViewportDragAction::MoveWindow), {}});
    settingsLibrary.insert("viewportverticalscrollaction", {static_cast<int>(Qv::ViewportScrollAction::Zoom), {}});
    settingsLibrary.insert("viewporthorizontalscrollaction", {static_cast<int>(Qv::ViewportScrollAction::Navigate), {}});
    settingsLibrary.insert("viewportaltverticalscrollaction", {static_cast<int>(Qv::ViewportScrollAction::Pan), {}});
    settingsLibrary.insert("viewportalthorizontalscrollaction", {static_cast<int>(Qv::ViewportScrollAction::Pan), {}});
    // Works best with touchpads that accurately report ScrollPhase (macOS only currently)
    settingsLibrary.insert("scrollactioncooldown", {true, {}});
    settingsLibrary.insert("cursorautohidefullscreenenabled", {true, {}});
    settingsLibrary.insert("cursorautohidefullscreendelay", {2, {}});
}
