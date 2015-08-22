// Copyright 2013-present Facebook. All Rights Reserved.

#include <pbxsetting/pbxsetting.h>
#include <xcsdk/xcsdk.h>
#include <pbxproj/pbxproj.h>
#include <pbxspec/pbxspec.h>

int
main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s developer project specs\n", argv[0]);
        return -1;
    }

    std::shared_ptr<xcsdk::SDK::Manager> xcsdk_manager = xcsdk::SDK::Manager::Open(argv[1]);
    if (!xcsdk_manager) {
        fprintf(stderr, "no developer dir found at %s\n", argv[1]);
        return -1;
    }

    auto project = pbxproj::PBX::Project::Open(argv[2]);
    if (!project) {
        fprintf(stderr, "error opening project at %s (%s)\n", argv[2], strerror(errno));
        return -1;
    }

    auto spec_manager = pbxspec::Manager::Open(nullptr, argv[3]);
    if (!spec_manager) {
        fprintf(stderr, "error opening specifications at %s (%s)\n", argv[3], strerror(errno));
        return -1;
    }

    printf("Project: %s\n", project->name().c_str());

    auto projectConfigurationList = project->buildConfigurationList();
    std::string projectConfigurationName = projectConfigurationList->defaultConfigurationName();
    auto projectConfiguration = *std::find_if(projectConfigurationList->begin(), projectConfigurationList->end(), [&](pbxproj::XC::BuildConfiguration::shared_ptr configuration) -> bool {
        return configuration->name() == projectConfigurationName;
    });
    printf("Project Configuration: %s\n", projectConfiguration->name().c_str());

    auto target = project->targets().front();
    printf("Target: %s\n", target->name().c_str());

    auto targetConfigurationList = target->buildConfigurationList();
    std::string targetConfigurationName = targetConfigurationList->defaultConfigurationName();
    auto targetConfiguration = *std::find_if(targetConfigurationList->begin(), targetConfigurationList->end(), [&](pbxproj::XC::BuildConfiguration::shared_ptr configuration) -> bool {
        return configuration->name() == targetConfigurationName;
    });
    printf("Target Configuration: %s\n", targetConfiguration->name().c_str());

    auto platform = *std::find_if(xcsdk_manager->platforms().begin(), xcsdk_manager->platforms().end(), [](std::shared_ptr<xcsdk::SDK::Platform> platform) -> bool {
        return platform->name() == "iphoneos";
    });
    printf("Platform: %s\n", platform->name().c_str());

    // NOTE(grp): Some platforms have specifications in other directories besides the primary Specifications folder.
    auto platformSpecifications = pbxspec::Manager::Open(spec_manager, platform->path() + "/Developer/Library/Xcode");

    auto sdk = platform->targets().front();
    printf("SDK: %s\n", sdk->displayName().c_str());

    // TODO(grp): Handle legacy targets.
    assert(target->isa() == pbxproj::PBX::NativeTarget::Isa());
    pbxproj::PBX::NativeTarget::shared_ptr nativeTarget = std::dynamic_pointer_cast<pbxproj::PBX::NativeTarget>(target);

    pbxspec::PBX::BuildSystem::shared_ptr buildSystem = spec_manager->buildSystem("com.apple.build-system.native");
    pbxspec::PBX::ProductType::shared_ptr productType = platformSpecifications->productType(nativeTarget->productType());
    // TODO(grp): Should this always use the first package type?
    pbxspec::PBX::PackageType::shared_ptr packageType = platformSpecifications->packageType(productType->packageTypes().at(0));

    pbxspec::PBX::Architecture::vector architectures = platformSpecifications->architectures();
    std::vector<pbxsetting::Setting> architectureSettings;
    std::vector<std::string> platformArchitectures;
    for (pbxspec::PBX::Architecture::shared_ptr architecture : architectures) {
        if (!architecture->architectureSetting().empty()) {
            architectureSettings.push_back(architecture->defaultSetting());
        }
        if (architecture->realArchitectures().empty()) {
            if (std::find(platformArchitectures.begin(), platformArchitectures.end(), architecture->identifier()) == platformArchitectures.end()) {
                platformArchitectures.push_back(architecture->identifier());
            }
        }
    }
    std::string platformArchitecturesValue;
    for (std::string const &arch : platformArchitectures) {
        if (&arch != &platformArchitectures[0]) {
            platformArchitecturesValue += " ";
        }
        platformArchitecturesValue += arch;
    }
    architectureSettings.push_back(pbxsetting::Setting::Parse("VALID_ARCHS", platformArchitecturesValue));
    pbxsetting::Level architectureLevel = pbxsetting::Level(architectureSettings);

    pbxsetting::Level config = pbxsetting::Level({
        pbxsetting::Setting::Parse("ACTION", "build"),
        pbxsetting::Setting::Parse("CONFIGURATION", targetConfigurationName),
        pbxsetting::Setting::Parse("CURRENT_ARCH", "arm64"), // TODO(grp): Should intersect VALID_ARCHS and ARCHS?
        pbxsetting::Setting::Parse("CURRENT_VARIANT", "normal"),
        pbxsetting::Setting::Parse("BUILD_COMPONENTS", "headers build"),
    });

    pbxsetting::Level targetSpecificationSettings = pbxsetting::Level({
        pbxsetting::Setting::Parse("PACKAGE_TYPE", packageType->identifier()),
        pbxsetting::Setting::Parse("PRODUCT_TYPE", productType->identifier()),
    });

    std::vector<pbxsetting::Level> levels;

    levels.push_back(config);

    // TODO(grp): targetConfiguration->baseConfigurationReference()
    levels.push_back(targetConfiguration->buildSettings());
    levels.push_back(target->settings());

    levels.push_back(targetSpecificationSettings);
    levels.push_back(packageType->defaultBuildSettings());
    levels.push_back(productType->defaultBuildProperties());

    // TODO(grp): projectConfiguration->baseConfigurationReference()
    levels.push_back(projectConfiguration->buildSettings());
    levels.push_back(project->settings());

    // TODO(grp): Figure out how these should be specified -- workspaces? how to pick a SRCROOT?
    // TODO(grp): Fix which settings are printed at the end -- appears to be ones with any override? But what about SED?
    levels.push_back(pbxsetting::Level({
        pbxsetting::Setting::Parse("CONFIGURATION_BUILD_DIR", "$(BUILD_DIR)/$(CONFIGURATION)$(EFFECTIVE_PLATFORM_NAME)"),
        pbxsetting::Setting::Parse("CONFIGURATION_TEMP_DIR", "$(PROJECT_TEMP_DIR)/$(CONFIGURATION)$(EFFECTIVE_PLATFORM_NAME)"),

        // HACK(grp): Hardcode a few paths for testing.
        pbxsetting::Setting::Parse("SYMROOT", "$(DERIVED_DATA_DIR)/$(PROJECT_NAME)-cpmowfgmqamrjrfvwivglsgfzkff/Build/Products"),
        pbxsetting::Setting::Parse("OBJROOT", "$(DERIVED_DATA_DIR)/$(PROJECT_NAME)-cpmowfgmqamrjrfvwivglsgfzkff/Build/Intermediates"),
        pbxsetting::Setting::Parse("SRCROOT", "/Users/grp/code/newsyc/HNKit"),
    }));

    levels.push_back(platform->overrideProperties());
    levels.push_back(sdk->customProperties());
    levels.push_back(sdk->settings());
    levels.push_back(platform->settings());
    levels.push_back(sdk->defaultProperties());
    levels.push_back(platform->defaultProperties());
    levels.push_back(xcsdk_manager->computedSettings());

    levels.push_back(pbxsetting::DefaultSettings::Environment());
    levels.push_back(pbxsetting::DefaultSettings::Internal());
    levels.push_back(pbxsetting::DefaultSettings::Local());
    levels.push_back(pbxsetting::DefaultSettings::System());
    levels.push_back(pbxsetting::DefaultSettings::Architecture());
    levels.push_back(pbxsetting::DefaultSettings::Build());

    levels.push_back(architectureLevel);
    levels.push_back(buildSystem->defaultSettings());

    pbxsetting::Environment environment = pbxsetting::Environment(levels, levels);

    pbxsetting::Condition condition = pbxsetting::Condition({
        { "sdk", sdk->canonicalName() },
        { "arch", environment.resolve("CURRENT_ARCH") },
        { "variant", environment.resolve("CURRENT_VARIANT") },
    });

    std::unordered_map<std::string, std::string> values = environment.computeValues(condition);
    std::map<std::string, std::string> orderedValues = std::map<std::string, std::string>(values.begin(), values.end());

    printf("\n\nBuild Settings:\n\n");
    for (auto const &value : orderedValues) {
        printf("    %s = %s\n", value.first.c_str(), value.second.c_str());
    }

    return 0;
}