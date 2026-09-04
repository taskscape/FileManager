using NUnit.Framework;

namespace FileManager.UiTests;

// This source-level contract keeps toolbar sizing configurable without coupling
// the new preference to menu row sizing or the application icon resources.
public sealed class ToolbarIconSizeContractTests
{
    [Test]
    public void Customize_toolbar_exposes_persisted_small_medium_and_large_icon_sizes()
    {
        var root = FindRepositoryRoot();
        var resourceIds = File.ReadAllText(Path.Combine(root, "src", "lang", "lang.rh"));
        var dialog = File.ReadAllText(Path.Combine(root, "src", "lang", "lang.rc"));
        var stringIds = File.ReadAllText(Path.Combine(root, "src", "texts.rh2"));
        var strings = File.ReadAllText(Path.Combine(root, "src", "lang", "texts.rc2"));
        var configuration = File.ReadAllText(Path.Combine(root, "src", "cfgdlg.h"));
        var configurationDefaults = File.ReadAllText(Path.Combine(root, "src", "dialogs_config_general.cpp"));
        var configurationStorage = File.ReadAllText(Path.Combine(root, "src", "mainwnd_config.cpp"));
        var customize = File.ReadAllText(Path.Combine(root, "src", "toolbar_dnd.cpp"));

        Assert.Multiple(() =>
        {
            Assert.That(resourceIds, Does.Contain("IDC_CTB_ICON_SIZE"));
            Assert.That(dialog, Does.Contain("&Icon size:"));
            Assert.That(dialog, Does.Contain("CBS_DROPDOWNLIST"));
            Assert.That(stringIds, Does.Contain("IDS_TOOLBAR_ICON_SIZE_SMALL"));
            Assert.That(stringIds, Does.Contain("IDS_TOOLBAR_ICON_SIZE_MEDIUM"));
            Assert.That(stringIds, Does.Contain("IDS_TOOLBAR_ICON_SIZE_LARGE"));
            Assert.That(strings, Does.Contain("Small (16x16 pixels)"));
            Assert.That(strings, Does.Contain("Medium (24x24 pixels)"));
            Assert.That(strings, Does.Contain("Large (32x32 pixels)"));
            Assert.That(configuration, Does.Contain("ToolbarIconSize"));
            Assert.That(configurationDefaults, Does.Contain("ToolbarIconSize = TOOLBAR_ICON_SIZE_SMALL"));
            Assert.That(configurationStorage, Does.Contain("CONFIG_TOOLBARICONSIZE_REG"));
            Assert.That(configurationStorage, Does.Contain("IsValidToolbarIconSize"));
            Assert.That(customize, Does.Contain("CBN_SELCHANGE"));
            Assert.That(customize, Does.Contain("ColorsChanged(TRUE, FALSE, FALSE)"));
        });
    }

    [Test]
    public void Toolbar_image_lists_scale_independently_from_menu_images_and_app_icon()
    {
        var root = FindRepositoryRoot();
        var constants = File.ReadAllText(Path.Combine(root, "src", "consts.h"));
        var globals = File.ReadAllText(Path.Combine(root, "src", "app_globals.cpp"));
        // Image-list construction moved with the graphics-focused implementation; keep this contract tied to its compiled owner.
        var graphics = File.ReadAllText(Path.Combine(root, "src", "app_graphics.cpp"));
        var svgRasterizer = File.ReadAllText(Path.Combine(root, "src", "svg.cpp"));
        var toolbarCore = File.ReadAllText(Path.Combine(root, "src", "toolbar_core.cpp"));
        var toolbarRendering = File.ReadAllText(Path.Combine(root, "src", "toolbar_rendering.cpp"));
        var menus = File.ReadAllText(Path.Combine(root, "src", "menu_templates.cpp"));
        var applicationResources = File.ReadAllText(Path.Combine(root, "src", "salamand.rc2"));

        Assert.Multiple(() =>
        {
            Assert.That(constants, Does.Contain("TOOLBAR_ICON_SIZE_SMALL = 16"));
            Assert.That(constants, Does.Contain("TOOLBAR_ICON_SIZE_MEDIUM = 24"));
            Assert.That(constants, Does.Contain("TOOLBAR_ICON_SIZE_LARGE = 32"));
            Assert.That(constants, Does.Contain("HGrayMenuImageList"));
            Assert.That(constants, Does.Contain("HHotMenuImageList"));
            Assert.That(globals, Does.Contain("HGrayMenuImageList = NULL"));
            Assert.That(globals, Does.Contain("HHotMenuImageList = NULL"));
            Assert.That(graphics, Does.Contain("GetToolbarIconSizeForSystemDPI()"));
            Assert.That(graphics, Does.Contain("ImageList_Create(toolbarIconSize, toolbarIconSize"));
            Assert.That(graphics, Does.Contain("ImageList_Create(menuIconSize, menuIconSize"));
            Assert.That(svgRasterizer, Does.Contain("iconSize / image->width"));
            Assert.That(svgRasterizer, Does.Contain("iconSize / image->height"));
            Assert.That(svgRasterizer, Does.Contain("(iconSize - image->width * scale) / 2"));
            Assert.That(svgRasterizer, Does.Not.Contain("float scale = sysDPIScale / 100;"),
                "Configured image-list cells must not retain a system-DPI-only 16-pixel glyph.");
            Assert.That(toolbarCore, Does.Contain("ImageWidth > 0 ? ImageWidth : GetIconSizeForSystemDPI"));
            Assert.That(toolbarCore, Does.Contain("ImageHeight > 0 ? ImageHeight : GetIconSizeForSystemDPI"));
            Assert.That(toolbarRendering, Does.Contain("item->HIcon, imgW, imgH"));
            Assert.That(toolbarRendering, Does.Not.Contain("item->HIcon, iconSize, iconSize"),
                "Direct HICON buttons must follow their toolbar image dimensions too.");
            Assert.That(menus, Does.Contain("HGrayMenuImageList, HHotMenuImageList"));
            Assert.That(applicationResources, Does.Contain("IDI_SALAMANDER ICON \"res\\\\salamand.ico\""),
                "Toolbar sizing must leave the main application icon resource unchanged.");
        });
    }

    [Test]
    public void Fluent_toolbar_spacing_scales_with_each_configured_icon_size()
    {
        var root = FindRepositoryRoot();
        var toolbarInterface = File.ReadAllText(Path.Combine(root, "src", "toolbar.h"));
        var toolbarCore = File.ReadAllText(Path.Combine(root, "src", "toolbar_core.cpp"));
        var toolbarRendering = File.ReadAllText(Path.Combine(root, "src", "toolbar_rendering.cpp"));
        var toolbarCustomize = File.ReadAllText(Path.Combine(root, "src", "toolbar_dnd.cpp"));
        var mainWindowCreation = File.ReadAllText(Path.Combine(root, "src", "mainwnd_messages.cpp"));
        var mainWindowRefresh = File.ReadAllText(Path.Combine(root, "src", "mainwnd_init.cpp"));

        // The Fluent family keeps a one-quarter-icon inset at 16, 24, and 32 pixels,
        // while startup and live size changes must apply the same geometry path.
        Assert.Multiple(() =>
        {
            Assert.That(toolbarInterface, Does.Contain("ApplyConfiguredIconSpacing"));
            Assert.That(toolbarCore, Does.Contain("(iconDimension + 3) / 4"));
            Assert.That(toolbarCore, Does.Contain("Padding.IconLeft + 1"));
            Assert.That(toolbarRendering, Does.Contain("max(TB_SP_WIDTH, 2 * (int)Padding.IconLeft)"));
            Assert.That(toolbarRendering, Does.Contain("max(2, (int)Padding.IconLeft / 2)"));
            Assert.That(toolbarCustomize, Does.Contain("iconSize + 2 * edgeSpacing"));
            Assert.That(toolbarCustomize, Does.Contain("r.left + edgeSpacing"));
            Assert.That(mainWindowCreation, Does.Contain("TopToolBar->ApplyConfiguredIconSpacing()"));
            Assert.That(mainWindowCreation, Does.Contain("MiddleToolBar->ApplyConfiguredIconSpacing()"));
            Assert.That(mainWindowRefresh, Does.Contain("TopToolBar->ApplyConfiguredIconSpacing()"));
            Assert.That(mainWindowRefresh, Does.Contain("MiddleToolBar->ApplyConfiguredIconSpacing()"));
        });
    }

    [Test]
    public void Go_to_hot_path_uses_a_conservative_saved_location_glyph()
    {
        var root = FindRepositoryRoot();
        var hotPathIcon = File.ReadAllText(Path.Combine(root, "src", "res", "toolbars", "GoToHotPath.svg"));

        // A Fluent bookmark expresses the saved destination at every optical size without tiny overlays.
        Assert.Multiple(() =>
        {
            Assert.That(hotPathIcon, Does.Contain("saved destination"));
            Assert.That(hotPathIcon, Does.Contain("fill=\"#0F6CBD\""));
            Assert.That(hotPathIcon, Does.Contain("bookmark_16_regular.svg"));
            Assert.That(hotPathIcon, Does.Not.Contain("#F7630C"));
            Assert.That(hotPathIcon, Does.Not.Contain("C10.5 14.5 12.5 12.5"));
        });
    }

    [Test]
    public void High_fidelity_reference_catalog_contains_every_runtime_toolbar_icon()
    {
        var root = FindRepositoryRoot();
        var runtimeDirectory = Path.Combine(root, "src", "res", "toolbars");
        var referenceDirectory = Path.Combine(root, "design", "icon-reference", "fluent-color-essentials");
        var referenceSvgDirectory = Path.Combine(referenceDirectory, "svg");
        var runtimeNames = Directory.GetFiles(runtimeDirectory, "*.svg").Select(Path.GetFileName).Order().ToArray();
        var referenceNames = Directory.GetFiles(referenceSvgDirectory, "*.svg").Select(Path.GetFileName).Order().ToArray();

        // The archival set must remain a complete, scalable mirror rather than a hand-picked presentation subset.
        Assert.That(referenceNames, Is.EqualTo(runtimeNames));
        // Preserve the original family while allowing complete drive-bar additions.
        Assert.That(referenceNames.Length, Is.GreaterThanOrEqualTo(91));
        foreach (var name in referenceNames)
        {
            var svg = File.ReadAllText(Path.Combine(referenceSvgDirectory, name!));
            Assert.That(svg, Does.Contain("width=\"64\" height=\"64\" viewBox=\"0 0 16 16\""), name);
        }

        var catalogPath = Path.Combine(referenceDirectory, "fluent-color-essentials-icon-catalog.png");
        var pngHeader = File.ReadAllBytes(catalogPath).Take(24).ToArray();
        int width = (pngHeader[16] << 24) | (pngHeader[17] << 16) | (pngHeader[18] << 8) | pngHeader[19];
        int height = (pngHeader[20] << 24) | (pngHeader[21] << 16) | (pngHeader[22] << 8) | pngHeader[23];
        Assert.Multiple(() =>
        {
            Assert.That(width, Is.EqualTo(3840));
            // Every row needs its full card height plus the catalog header and footer.
            Assert.That(height, Is.EqualTo(240 + (int)Math.Ceiling(runtimeNames.Length / 7.0) * 240 + 80));
        });
    }

    private static string FindRepositoryRoot()
    {
        var current = new DirectoryInfo(TestContext.CurrentContext.TestDirectory);
        while (current is not null && !Directory.Exists(Path.Combine(current.FullName, "src")))
            current = current.Parent;

        return current?.FullName
            ?? throw new DirectoryNotFoundException("Could not locate the FileManager repository root.");
    }
}
