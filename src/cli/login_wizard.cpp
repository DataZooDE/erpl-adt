#include <erpl_adt/cli/login_wizard.hpp>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>

namespace erpl_adt {

namespace {

// Filter input to allow only digit characters.
ftxui::ComponentDecorator DigitsOnly() {
    return ftxui::CatchEvent([](ftxui::Event event) {
        return event.is_character() && !std::isdigit(event.character()[0]);
    });
}

uint16_t ParsePortOrDefault(const std::string& port_str, uint16_t default_port) {
    if (port_str.empty()) {
        return default_port;
    }
    char* end = nullptr;
    errno = 0;
    long value = std::strtol(port_str.c_str(), &end, 10);
    if (end == port_str.c_str() || *end != '\0' || errno == ERANGE ||
        value <= 0 || value > std::numeric_limits<uint16_t>::max()) {
        return default_port;
    }
    return static_cast<uint16_t>(value);
}

} // namespace

std::optional<LoginCredentials> RunLoginWizard(
    const std::optional<LoginCredentials>& defaults) {
    using namespace ftxui;

    // Field values.
    std::string host = defaults ? defaults->host : "localhost";
    std::string port_str =
        defaults ? std::to_string(defaults->port) : "50000";
    std::string user = defaults ? defaults->user : "DEVELOPER";
    std::string password;  // Never pre-fill password.
    std::string client = defaults ? defaults->client : "001";
    bool use_https = defaults ? defaults->use_https : false;

    bool saved = false;

    auto screen = ScreenInteractive::FitComponent();

    // Input components.
    auto host_input = Input(&host, "hostname");
    auto port_input = Input(&port_str, "port") | DigitsOnly();
    auto user_input = Input(&user, "username");

    InputOption password_opt;
    password_opt.password = true;
    auto password_input = Input(&password, "password", password_opt);

    auto client_input = Input(&client, "client");
    auto https_checkbox = Checkbox("Use HTTPS", &use_https);

    // Buttons.
    auto save_button = Button("  Save  ", [&] {
        saved = true;
        screen.Exit();
    });
    auto cancel_button = Button(" Cancel ", [&] { screen.Exit(); });

    // Layout.
    auto form = Container::Vertical({
        host_input,
        port_input,
        user_input,
        password_input,
        client_input,
        https_checkbox,
        Container::Horizontal({save_button, cancel_button}),
    });

    // Escape to cancel.
    form |= CatchEvent([&](Event event) {
        if (event == Event::Escape) {
            screen.Exit();
            return true;
        }
        return false;
    });

    // Renderer for labels + border.
    auto renderer = Renderer(form, [&] {
        auto label_width = 12;
        auto make_row = [&](const std::string& label, Element field) {
            return hbox({
                text(label) | size(WIDTH, EQUAL, label_width),
                field | flex,
            });
        };

        return vbox({
                   text("SAP Connection Setup") | bold | center,
                   separator(),
                   make_row("Host:", host_input->Render()),
                   make_row("Port:", port_input->Render()),
                   make_row("User:", user_input->Render()),
                   make_row("Password:", password_input->Render()),
                   make_row("Client:", client_input->Render()),
                   hbox({
                       text("") | size(WIDTH, EQUAL, label_width),
                       https_checkbox->Render(),
                   }),
                   separator(),
                   hbox({
                       save_button->Render(),
                       text("  "),
                       cancel_button->Render(),
                   }) | center,
               }) |
               border | size(WIDTH, LESS_THAN, 60);
    });

    screen.Loop(renderer);

    if (!saved) {
        return std::nullopt;
    }

    LoginCredentials creds;
    creds.host = host;
    creds.port = ParsePortOrDefault(port_str, 50000);
    creds.user = user;
    creds.password = password;
    creds.client = client.empty() ? "001" : client;
    creds.use_https = use_https;
    return creds;
}

} // namespace erpl_adt
