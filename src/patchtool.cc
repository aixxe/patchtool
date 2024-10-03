#include <regex>

using namespace std::string_view_literals;

struct opts_t
{
    std::filesystem::path input;
    std::filesystem::path profile;
    std::filesystem::path output;
    bool no_verify = false;
    bool union_all_opts = false;
};

struct byte_patch
{
    std::string rva;
    std::string on;
    std::string off;
};

struct default_patch
{
    std::vector<byte_patch> patches;
};

struct union_option
{
    std::string name;
    std::string bytes;
};

struct union_patch
{
    std::string rva;
    std::string off;
    std::vector<union_option> options;
};

struct number_patch
{
    std::string rva;
    std::string off;
    std::int32_t min;
    std::int32_t max;
    std::uint32_t size;

    [[nodiscard]] auto encode(auto value) const -> std::string
    {
        auto constexpr hex = "0123456789ABCDEF";

        auto result = std::string {};
        result.reserve(size * 2);

        for (auto i = 0; i < size; ++i)
        {
            auto const byte = static_cast<std::uint8_t>(value >> 8 * i & 0xFF);
            result.push_back(hex[byte >> 4]);
            result.push_back(hex[byte & 0x0F]);
        }

        return result;
    }
};

struct container_t
{
    using patch_t = std::variant<default_patch, union_patch, number_patch>;
    using patch_store_t = std::unordered_map<std::string, patch_t>;

    std::string file;
    patch_store_t patches;
    std::vector<std::string> validate;
};

struct profile_t
{
    std::filesystem::path path;
    std::vector<std::pair<std::string, std::string>> patches;
};

auto parse_argv(int argc, char* argv[])
{
    auto args = std::optional<opts_t> {};
    auto parser = args::ArgumentParser { "organization utility for mempatch-hook format patches" };

    auto help = args::HelpFlag
        { parser, "help", "print usage information", {"help"} };

    auto input = args::ValueFlag<std::string>
        { parser, "path", "input .json metadata", {'i', "input"}, args::Options::Required };
    auto profile = args::ValueFlag<std::string>
        { parser, "path", "input .yml split file", {'p', "profile"}, args::Options::Required };
    auto output = args::ValueFlag<std::string>
        { parser, "path", "split output directory", {'o', "output"}, args::Options::Required };
    auto no_verify = args::Flag
        { parser, "no-verify", "exclude original patch bytes", {'N', "no-verify"} };
    auto union_all_opts = args::Flag
        { parser, "union-all-opts", "include all union options", {'U', "union-all-opts"} };

    if (argc == 1)
        std::cout << parser;

    try
    {
        parser.ParseCLI(argc, argv);

        auto result = opts_t
        {
            .input   = get(input),
            .profile = get(profile),
            .output  = std::filesystem::absolute(get(output)),
        };

        if (!exists(result.input))
            throw args::Error { "input does not exist" };
        if (!exists(result.profile))
            throw args::Error { "profile does not exist" };
        if (!exists(result.output) && !create_directories(result.output))
            throw args::Error { "failed to create output dir" };

        result.no_verify = get(no_verify);
        result.union_all_opts = get(union_all_opts);

        args = result;
    }
    catch (const args::Help&)
        { std::cout << parser; }

    return args;
}

auto parse_metadata(const opts_t& options)
{
    auto input = std::ifstream { options.input, std::ios::ate };

    if (!input.is_open())
        throw std::runtime_error { "failed to open input file" };

    auto buffer = std::string(input.tellg(), '\0');
    auto document = rapidjson::Document {};

    input.seekg(0);
    input.read(buffer.data(), buffer.size());

    document.Parse(buffer.c_str());

    if (document.HasParseError())
        throw std::runtime_error { "failed to parse input file" };

    auto container = container_t
        { .file = document["info"]["file"].GetString() };

    if (document["info"].HasMember("mempatcherValidate"))
    {
        auto const& validate = document["info"]["mempatcherValidate"];

        if (validate.IsArray())
            for (auto const& item: validate.GetArray())
                container.validate.emplace_back(item.GetString());
    }

    auto parse_address = [] (auto&& value) -> std::string
    {
        auto result = std::string { value };

        if (result.starts_with("0x"))
            result.erase(0, 2);

        return result;
    };

    for (auto&& [key, value]: document["data"].GetObject())
    {
        auto const name = key.GetString();
        auto const type = value["type"].GetString();

        if (type == "default"sv)
        {
            auto result = default_patch {};

            for (auto&& item: value["patches"].GetArray())
            {
                auto const rva = parse_address(item["rva"].GetString());
                auto const off = item["off"].GetString();
                auto const on = item["on"].GetString();

                result.patches.emplace_back(rva, on, off);
            }

            container.patches[name] = std::move(result);
        }
        else if (type == "union"sv)
        {
            auto result = union_patch { .off = value["default"].GetString() };

            for (auto&& [option, bytes]: value["patches"].GetObject())
            {
                if (option.GetString() == "rva"sv)
                    result.rva = parse_address(bytes.GetString());

                if (option.GetString() == "offset"sv || option.GetString() == "rva"sv)
                    continue;

                result.options.emplace_back(option.GetString(), bytes.GetString());
            }

            container.patches[name] = std::move(result);
        }
        else if (type == "number"sv)
        {
            auto result = number_patch
            {
                .rva = parse_address(value["patches"]["rva"].GetString()),
                .off = value["patches"]["off"].GetString(),
                .min = value["patches"]["min"].GetInt(),
                .max = value["patches"]["max"].GetInt(),
                .size = value["patches"]["size"].GetUint(),
            };

            container.patches[name] = std::move(result);
        }
    }

    return container;
}

auto parse_profiles(const opts_t& options)
{
    auto const yml = YAML::LoadFile(options.profile.string());
    auto result = std::vector<profile_t> {};

    if (!yml.IsMap())
        throw std::runtime_error { "unexpected profile format" };

    for (auto lists = yml.begin(); lists != yml.end(); ++lists)
    {
        auto fname = lists->first.as<std::string>();
        auto const patches = lists->second;

        if (!patches.IsSequence())
        {
            std::println("warn: unexpected node type for patch list '{}'", fname);
            continue;
        }

        for (auto&& [token, replacement]: std::vector<std::pair<std::regex, std::string>> {
            { std::regex(R"(\$\{INPUT_FILENAME\})"), options.input.filename().replace_extension("").string() },
            { std::regex(R"(\$\{PROFILE_FILENAME\})"), options.profile.filename().replace_extension("").string() },
        }) { fname = std::regex_replace(fname, token, replacement); }

        auto profile = profile_t { .path = options.output / fname };

        for (auto patch = patches.begin(); patch != patches.end(); ++patch)
        {
            profile.patches.emplace_back(
                patch->begin()->first.as<std::string>(),
                patch->begin()->second.as<std::string>()
            );
        }

        result.emplace_back(profile);
    }

    return result;
}

auto main(int argc, char* argv[]) -> int try
{
    auto const opts = parse_argv(argc, argv);

    if (!opts)
        return EXIT_SUCCESS;

    auto const container = parse_metadata(*opts);
    auto const profiles = parse_profiles(*opts);

    std::println("read {} patches from input file '{}'...",
        container.patches.size(), opts->input.string());

    auto unmatched = container.patches
                   | std::views::keys
                   | std::ranges::to<std::vector<std::string>>();

    for (auto const& profile: profiles)
    {
        auto buffer = std::string {};
        auto static once = std::once_flag {};

        std::call_once(once, [&]
        {
            for (auto const& line: container.validate)
                buffer += line + "\n";
        });

        for (auto const& [name, value]: profile.patches)
        {
            auto const patch = container.patches.find(name);

            if (patch == container.patches.end())
            {
                std::println(std::cerr, "warn: patch '{}' in profile '{}' not found...",
                    name, profile.path.filename().string());
                continue;
            }

            std::erase(unmatched, name);

            buffer += std::format("\n## {}\n", name);

            std::visit([&] (auto&& i)
            {
                using T = std::decay_t<decltype(i)>;

                if constexpr (std::is_same_v<T, default_patch>)
                {
                    auto const enabled = value == "1" || value == "on" || value == "true";
                    for (auto const& item: i.patches)
                        buffer += (enabled ? "": "# ")
                               + std::format("{} {} {}", container.file, item.rva, item.on)
                               + (opts->no_verify ? "": std::format(" {}", item.off)) + "\n";
                }
                else if constexpr (std::is_same_v<T, union_patch>)
                {
                    for (auto const& item: i.options)
                    {
                        auto const enabled = (value == item.name);
                        if (!enabled && !opts->union_all_opts)
                            continue;
                        buffer += "### " + item.name + "\n";
                        buffer += (enabled ? "": "# ")
                               + std::format("{} {} {}", container.file, i.rva, item.bytes)
                               + (opts->no_verify ? "": std::format(" {}", i.off)) + "\n";
                    }
                }
                else if constexpr (std::is_same_v<T, number_patch>)
                {
                    auto number = std::stoll(value);
                    if (number < i.min || number > i.max)
                        throw std::runtime_error { "number patch value out of range" };
                    buffer += std::format("{} {} {}", container.file, i.rva, i.encode(number))
                           + (opts->no_verify ? "": std::format(" {}", i.off)) + "\n";
                }
            }, patch->second);
        }

        if (buffer.empty())
            continue;

        std::println("writing {} patches to '{}...",
            profile.patches.size(), profile.path.string());

        if (buffer.front() == '\n')
            buffer.erase(0, 1);

        std::ofstream { profile.path, std::ios::trunc } << buffer;
    }

    for (auto const& patch: unmatched)
        std::println(std::cerr, "warn: patch '{}' not used in profile", patch);

    return EXIT_SUCCESS;
}
catch (std::exception const& e)
{
    std::cerr << "fatal: " << e.what() << std::endl;
    return EXIT_FAILURE;
}