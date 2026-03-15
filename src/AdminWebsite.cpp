#include "AdminWebsite.h"

#include "Application.h"
#include "Session.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// ── CSS ──────────────────────────────────────────────────────────────────────

const std::string CSS = R"css(
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: 'Segoe UI', system-ui, sans-serif; background: #1a1a2e; color: #e0e0e0; min-height: 100vh; }
a { color: #7eb8f7; text-decoration: none; }
a:hover { text-decoration: underline; }

nav {
    background: #0f3460;
    padding: 12px 24px;
    display: flex;
    align-items: center;
    gap: 16px;
    border-bottom: 2px solid #e94560;
}
nav .brand { font-size: 1.2rem; font-weight: 700; color: #e0e0e0; letter-spacing: 1px; }
nav .breadcrumb { font-size: 0.85rem; color: #aaa; }
nav .breadcrumb a { color: #7eb8f7; }

.container { max-width: 1200px; margin: 0 auto; padding: 24px 16px; }

.card {
    background: #16213e;
    border: 1px solid #0f3460;
    border-radius: 8px;
    padding: 20px;
    margin-bottom: 20px;
}
.card h2 { font-size: 1rem; font-weight: 600; color: #7eb8f7; margin-bottom: 14px; text-transform: uppercase; letter-spacing: 0.5px; border-bottom: 1px solid #0f3460; padding-bottom: 8px; }

table { width: 100%; border-collapse: collapse; }
th { text-align: left; font-size: 0.78rem; color: #888; text-transform: uppercase; padding: 6px 10px; border-bottom: 1px solid #0f3460; }
td { padding: 10px 10px; border-bottom: 1px solid #0d1117; font-size: 0.9rem; vertical-align: middle; }
tr:last-child td { border-bottom: none; }
tr:hover td { background: #1a2a4a; }

.badge {
    display: inline-block;
    padding: 2px 10px;
    border-radius: 12px;
    font-size: 0.75rem;
    font-weight: 600;
    letter-spacing: 0.3px;
}
.badge-green  { background: #1a3d2b; color: #4ecca3; border: 1px solid #4ecca3; }
.badge-yellow { background: #3d3010; color: #f5c842; border: 1px solid #f5c842; }
.badge-orange { background: #3d2010; color: #f5a623; border: 1px solid #f5a623; }
.badge-red    { background: #3d0f1a; color: #e94560; border: 1px solid #e94560; }
.badge-gray   { background: #2a2a2a; color: #888;    border: 1px solid #555; }
.badge-blue   { background: #0f2a3d; color: #7eb8f7; border: 1px solid #7eb8f7; }

.dot { display: inline-block; width: 10px; height: 10px; border-radius: 50%; margin-right: 6px; }
.dot-green  { background: #4ecca3; }
.dot-red    { background: #e94560; }
.dot-yellow { background: #f5c842; }

.btn {
    display: inline-block;
    padding: 7px 16px;
    border-radius: 5px;
    border: none;
    font-size: 0.85rem;
    font-weight: 600;
    cursor: pointer;
    transition: opacity 0.15s;
    color: #e0e0e0;
    background: #0f3460;
}
.btn:hover { opacity: 0.85; }
.btn-green  { background: #1a4a30; color: #4ecca3; border: 1px solid #4ecca3; }
.btn-red    { background: #4a1020; color: #e94560; border: 1px solid #e94560; }
.btn-orange { background: #4a2010; color: #f5a623; border: 1px solid #f5a623; }
.btn-blue   { background: #0f2a3d; color: #7eb8f7; border: 1px solid #7eb8f7; }

.controls { display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }
.controls form { display: inline; }

.seqnum-form { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; margin-top: 14px; padding-top: 14px; border-top: 1px solid #0f3460; }
.seqnum-form label { font-size: 0.85rem; color: #aaa; }
.seqnum-form input[type=number] {
    background: #0d1117; border: 1px solid #0f3460; color: #e0e0e0;
    padding: 6px 10px; border-radius: 4px; width: 110px; font-size: 0.85rem;
}
.seqnum-form input[type=number]:focus { outline: none; border-color: #7eb8f7; }

.log-view {
    background: #0d1117;
    border: 1px solid #0f3460;
    border-radius: 6px;
    padding: 12px;
    font-family: 'Cascadia Code', 'Fira Code', 'Consolas', monospace;
    font-size: 0.78rem;
    color: #b0c4de;
    overflow-y: auto;
    max-height: 600px;
    line-height: 1.6;
}
.log-line { padding: 2px 6px; border-radius: 3px; margin-bottom: 2px; border-left: 3px solid transparent; }
.log-line:hover { background: #1a2233; }
.log-sent { border-left-color: #7eb8f7; }
.log-recv { border-left-color: #4ecca3; }
.log-event { border-left-color: #555; }

.dir-badge {
    display: inline-block;
    padding: 1px 6px;
    border-radius: 3px;
    font-size: 0.7rem;
    font-weight: 700;
    margin-right: 6px;
    vertical-align: middle;
}
.dir-sent { background: #0f2a3d; color: #7eb8f7; }
.dir-recv { background: #1a3d2b; color: #4ecca3; }

.log-controls { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; margin-bottom: 14px; }
.log-controls select, .log-controls input[type=text] {
    background: #0d1117; border: 1px solid #0f3460; color: #e0e0e0;
    padding: 6px 10px; border-radius: 4px; font-size: 0.82rem;
}
.log-controls label { font-size: 0.82rem; color: #aaa; }

.config-key { color: #888; font-size: 0.82rem; }
.config-val { font-size: 0.85rem; }

.empty { color: #555; font-style: italic; }

.stat-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(160px, 1fr)); gap: 14px; }
.stat-item { background: #0d1117; border-radius: 6px; padding: 14px; }
.stat-label { font-size: 0.72rem; color: #888; text-transform: uppercase; letter-spacing: 0.5px; margin-bottom: 6px; }
.stat-value { font-size: 1.2rem; font-weight: 700; }

.page-title { font-size: 1.4rem; font-weight: 700; margin-bottom: 20px; color: #e0e0e0; }
.not-found { text-align: center; padding: 60px 20px; color: #555; font-size: 1.1rem; }
)css";

// ── Helpers ───────────────────────────────────────────────────────────────────

std::string htmlEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;";  break;
            case '<': out += "&lt;";   break;
            case '>': out += "&gt;";   break;
            case '"': out += "&quot;"; break;
            default:  out += c;
        }
    }
    return out;
}

std::string formatFIXMessage(const std::string& raw)
{
    std::string r = raw;
    std::replace(r.begin(), r.end(), '\x01', '|');
    return htmlEscape(r);
}

std::string stateToString(SessionState s)
{
    switch (s) {
        case SessionState::LOGON:        return "LOGON";
        case SessionState::READY:        return "READY";
        case SessionState::TEST_REQUEST: return "TEST_REQUEST";
        case SessionState::KILLING:      return "KILLING";
        case SessionState::LOGOUT:       return "LOGOUT";
    }
    return "UNKNOWN";
}

std::string stateToBadgeClass(SessionState s)
{
    switch (s) {
        case SessionState::READY:        return "badge-green";
        case SessionState::LOGON:        return "badge-yellow";
        case SessionState::TEST_REQUEST: return "badge-yellow";
        case SessionState::LOGOUT:       return "badge-orange";
        case SessionState::KILLING:      return "badge-red";
    }
    return "badge-gray";
}

std::string sessionTypeString(const SessionSettings& settings)
{
    try {
        auto t = settings.getSessionType();
        if (t == SessionType::INITIATOR) return "Initiator";
        if (t == SessionType::ACCEPTOR)  return "Acceptor";
    } catch (...) {}
    return "Unknown";
}

std::string getLogBase(const Session& session)
{
    const auto& settings = session.getSettings();
    return PlatformSettings::getString(PlatformSettings::LOG_PATH)
        + "/" + settings.getString(SessionSettings::SENDER_COMP_ID)
        + "-" + settings.getString(SessionSettings::TARGET_COMP_ID);
}

std::vector<std::string> readLogTail(const std::string& path, int maxLines, const std::string& filter)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        return {};

    auto fileSize = file.tellg();
    if (fileSize == 0)
        return {};

    const std::streamoff readSize = std::min(static_cast<std::streamoff>(fileSize), static_cast<std::streamoff>(512 * 1024));
    file.seekg(-readSize, std::ios::end);

    std::string buffer(readSize, '\0');
    file.read(buffer.data(), readSize);

    std::vector<std::string> lines;
    std::istringstream stream(buffer);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty())
            continue;
        if (filter.empty() || line.find(filter) != std::string::npos)
            lines.push_back(line);
    }

    if (static_cast<int>(lines.size()) > maxLines)
        lines.erase(lines.begin(), lines.begin() + (lines.size() - maxLines));

    return lines;
}

std::unordered_map<std::string, std::string> parseFormBody(const std::string& body)
{
    std::unordered_map<std::string, std::string> result;
    std::istringstream stream(body);
    std::string token;
    while (std::getline(stream, token, '&')) {
        auto eq = token.find('=');
        if (eq != std::string::npos)
            result[token.substr(0, eq)] = token.substr(eq + 1);
    }
    return result;
}

std::string buildPage(const std::string& title, const std::string& navExtra, const std::string& body, bool autoRefresh = false, int refreshMs = 5000)
{
    std::ostringstream out;
    out << "<!DOCTYPE html><html lang=\"en\"><head>"
        << "<meta charset=\"utf-8\">"
        << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        << "<title>" << htmlEscape(title) << " - openfix Admin</title>"
        << "<style>" << CSS << "</style>"
        << "</head><body>"
        << "<nav>"
        << "<span class=\"brand\">openfix</span>"
        << navExtra
        << "</nav>"
        << "<div class=\"container\">"
        << body
        << "</div>";
    if (autoRefresh) {
        out << "<script>"
            << "setTimeout(function(){window.location.reload();}," << refreshMs << ");"
            << "window.addEventListener('load',function(){window.scrollTo(0,document.body.scrollHeight);});"
            << "</script>";
    }
    out << "</body></html>";
    return out.str();
}

std::string notFoundPage(const std::string& sessionName)
{
    std::string body = "<div class=\"not-found\"><h2>Session not found: " + htmlEscape(sessionName) + "</h2>"
                     + "<p style=\"margin-top:12px\"><a href=\"/\">Back to dashboard</a></p></div>";
    return buildPage("Not Found", "<span class=\"breadcrumb\"><a href=\"/\">Dashboard</a></span>", body);
}

} // namespace

// ── AdminWebsite ──────────────────────────────────────────────────────────────

AdminWebsite::AdminWebsite(Application& app, int port)
    : m_app(app), m_port(port)
{
    start();
}

AdminWebsite::~AdminWebsite()
{
    m_website.stop();
    m_thread.join();
}

void AdminWebsite::start()
{
    LOG_INFO("Starting with port: " << m_port);

    // ── Dashboard ────────────────────────────────────────────────────────────
    CROW_ROUTE(m_website, "/")([this](const crow::request&, crow::response& res) {
        std::ostringstream body;
        body << "<div class=\"page-title\">Sessions</div>";
        body << "<div class=\"card\">";

        if (m_app.m_sessionMap.empty()) {
            body << "<p class=\"empty\">No sessions configured.</p>";
        } else {
            body << "<table>"
                 << "<thead><tr>"
                 << "<th>Name</th><th>Type</th><th>FIX Version</th>"
                 << "<th>State</th><th>Connected</th><th>Enabled</th>"
                 << "<th>Sender SeqNum</th><th>Target SeqNum</th>"
                 << "</tr></thead><tbody>";

            for (auto& [name, session] : m_app.m_sessionMap) {
                const auto& settings = session->getSettings();
                auto state = session->getState();
                bool connected = session->getNetwork()->isConnected();
                bool enabled   = session->isEnabled();

                body << "<tr>"
                     << "<td><a href=\"/session/" << htmlEscape(name) << "\">" << htmlEscape(name) << "</a></td>"
                     << "<td>" << sessionTypeString(settings) << "</td>"
                     << "<td>" << htmlEscape(settings.getString(SessionSettings::BEGIN_STRING)) << "</td>"
                     << "<td><span class=\"badge " << stateToBadgeClass(state) << "\">" << stateToString(state) << "</span></td>"
                     << "<td><span class=\"dot " << (connected ? "dot-green" : "dot-red") << "\"></span>" << (connected ? "Yes" : "No") << "</td>"
                     << "<td><span class=\"dot " << (enabled ? "dot-green" : "dot-red") << "\"></span>" << (enabled ? "Yes" : "No") << "</td>"
                     << "<td>" << session->getSenderSeqNum() << "</td>"
                     << "<td>" << session->getTargetSeqNum() << "</td>"
                     << "</tr>";
            }
            body << "</tbody></table>";
        }
        body << "</div>";

        res.body = buildPage("Dashboard", "", body.str(), true, 5000);
        res.end();
    });

    // ── Session Detail ───────────────────────────────────────────────────────
    CROW_ROUTE(m_website, "/session/<string>")([this](const crow::request&, crow::response& res, std::string name) {
        auto it = m_app.m_sessionMap.find(name);
        if (it == m_app.m_sessionMap.end()) {
            res.code = 404;
            res.body = notFoundPage(name);
            res.end();
            return;
        }

        auto& session = *it->second;
        const auto& settings = session.getSettings();
        auto state     = session.getState();
        bool connected = session.getNetwork()->isConnected();
        bool enabled   = session.isEnabled();
        std::string enc = htmlEscape(name);

        std::ostringstream body;

        // Nav
        std::string nav = "<span class=\"breadcrumb\"><a href=\"/\">Dashboard</a> &rsaquo; " + enc + "</span>";

        // Status card
        body << "<div class=\"page-title\">" << enc << "</div>";
        body << "<div class=\"card\"><h2>Status</h2><div class=\"stat-grid\">";
        body << "<div class=\"stat-item\"><div class=\"stat-label\">State</div>"
             << "<div class=\"stat-value\"><span class=\"badge " << stateToBadgeClass(state) << "\">" << stateToString(state) << "</span></div></div>";
        body << "<div class=\"stat-item\"><div class=\"stat-label\">Connected</div>"
             << "<div class=\"stat-value\"><span class=\"dot " << (connected ? "dot-green" : "dot-red") << "\"></span>" << (connected ? "Yes" : "No") << "</div></div>";
        body << "<div class=\"stat-item\"><div class=\"stat-label\">Enabled</div>"
             << "<div class=\"stat-value\"><span class=\"dot " << (enabled ? "dot-green" : "dot-red") << "\"></span>" << (enabled ? "Yes" : "No") << "</div></div>";
        body << "<div class=\"stat-item\"><div class=\"stat-label\">Sender SeqNum</div>"
             << "<div class=\"stat-value\">" << session.getSenderSeqNum() << "</div></div>";
        body << "<div class=\"stat-item\"><div class=\"stat-label\">Target SeqNum</div>"
             << "<div class=\"stat-value\">" << session.getTargetSeqNum() << "</div></div>";
        body << "</div></div>";

        // Controls card
        body << "<div class=\"card\"><h2>Controls</h2>";
        body << "<div class=\"controls\">";

        // Enable
        body << "<form method=\"post\" action=\"/session/" << enc << "/enable\">"
             << "<button class=\"btn btn-green\" type=\"submit\">Enable</button></form>";
        // Disable
        body << "<form method=\"post\" action=\"/session/" << enc << "/disable\">"
             << "<button class=\"btn btn-orange\" type=\"submit\">Disable</button></form>";
        // Disconnect
        body << "<form method=\"post\" action=\"/session/" << enc << "/disconnect\">"
             << "<button class=\"btn btn-red\" type=\"submit\">Force Disconnect</button></form>";

        body << "</div>";

        // Set seq nums
        body << "<form method=\"post\" action=\"/session/" << enc << "/seqnum\">"
             << "<div class=\"seqnum-form\">"
             << "<label>Sender SeqNum: <input type=\"number\" name=\"sender\" min=\"1\" value=\"" << session.getSenderSeqNum() << "\"></label>"
             << "<label>Target SeqNum: <input type=\"number\" name=\"target\" min=\"1\" value=\"" << session.getTargetSeqNum() << "\"></label>"
             << "<button class=\"btn btn-blue\" type=\"submit\">Apply</button>"
             << "</div></form>";

        body << "</div>";

        // Log viewers card
        body << "<div class=\"card\"><h2>Logs</h2><div class=\"controls\">";
        body << "<a class=\"btn btn-blue\" href=\"/session/" << enc << "/messages\">Message Log</a>";
        body << "<a class=\"btn btn-blue\" href=\"/session/" << enc << "/events\">Event Log</a>";
        body << "</div></div>";

        // Configuration card
        body << "<div class=\"card\"><h2>Configuration</h2><table>";
        body << "<thead><tr><th>Setting</th><th>Value</th></tr></thead><tbody>";

        auto row = [&](const std::string& key, const std::string& val) {
            std::string display = val.empty() ? "<span class=\"empty\">&mdash;</span>" : htmlEscape(val);
            body << "<tr><td class=\"config-key\">" << key << "</td><td class=\"config-val\">" << display << "</td></tr>";
        };
        auto brow = [&](const std::string& key, bool val) {
            row(key, val ? "Yes" : "No");
        };
        auto lrow = [&](const std::string& key, long val) {
            row(key, std::to_string(val));
        };

        row("BeginString",             settings.getString(SessionSettings::BEGIN_STRING));
        row("SenderCompID",            settings.getString(SessionSettings::SENDER_COMP_ID));
        row("TargetCompID",            settings.getString(SessionSettings::TARGET_COMP_ID));
        row("SessionType",             settings.getString(SessionSettings::SESSION_TYPE_STR));
        row("ConnectHost",             settings.getString(SessionSettings::CONNECT_HOST));
        lrow("ConnectPort",            settings.getLong(SessionSettings::CONNECT_PORT));
        lrow("AcceptPort",             settings.getLong(SessionSettings::ACCEPT_PORT));
        lrow("HeartbeatInterval",      settings.getLong(SessionSettings::HEARTBEAT_INTERVAL));
        lrow("LogonInterval",          settings.getLong(SessionSettings::LOGON_INTERVAL));
        lrow("ReconnectInterval",      settings.getLong(SessionSettings::RECONNECT_INTERVAL));
        lrow("ConnectTimeout",         settings.getLong(SessionSettings::CONNECT_TIMEOUT));
        brow("TLSEnabled",             settings.getBool(SessionSettings::TLS_ENABLED));
        brow("TLSVerifyPeer",          settings.getBool(SessionSettings::TLS_VERIFY_PEER));
        brow("TLSRequireClientCert",   settings.getBool(SessionSettings::TLS_REQUIRE_CLIENT_CERT));
        row("TLSServerName",           settings.getString(SessionSettings::TLS_SERVER_NAME));
        brow("ResetSeqNumOnLogon",     settings.getBool(SessionSettings::RESET_SEQ_NUM_ON_LOGON));
        brow("AllowResetSeqNumFlag",   settings.getBool(SessionSettings::ALLOW_RESET_SEQ_NUM_FLAG));
        brow("SendNextExpectedMsgSeqNum", settings.getBool(SessionSettings::SEND_NEXT_EXPECTED_MSG_SEQ_NUM));
        brow("ValidateRequiredFields", settings.getBool(SessionSettings::VALIDATE_REQUIRED_FIELDS));
        brow("RelaxedParsing",         settings.getBool(SessionSettings::RELAXED_PARSING));
        brow("TCPNoDelay",             settings.getBool(SessionSettings::ENABLE_TCP_NODELAY));
        brow("TCPQuickAck",            settings.getBool(SessionSettings::ENABLE_TCP_QUICKACK));
        row("FIXDictionary",           settings.getString(SessionSettings::FIX_DICTIONARY));
        row("StartTime",               settings.getString(SessionSettings::START_TIME));
        row("StopTime",                settings.getString(SessionSettings::STOP_TIME));

        body << "</tbody></table></div>";

        res.body = buildPage(name, nav, body.str(), true, 5000);
        res.end();
    });

    // ── Enable ───────────────────────────────────────────────────────────────
    CROW_ROUTE(m_website, "/session/<string>/enable").methods("POST"_method)(
        [this](const crow::request&, crow::response& res, std::string name) {
            auto it = m_app.m_sessionMap.find(name);
            if (it != m_app.m_sessionMap.end())
                it->second->setEnabled(true);
            res.moved("/session/" + name);
            res.end();
        });

    // ── Disable ──────────────────────────────────────────────────────────────
    CROW_ROUTE(m_website, "/session/<string>/disable").methods("POST"_method)(
        [this](const crow::request&, crow::response& res, std::string name) {
            auto it = m_app.m_sessionMap.find(name);
            if (it != m_app.m_sessionMap.end())
                it->second->setEnabled(false);
            res.moved("/session/" + name);
            res.end();
        });

    // ── Disconnect ───────────────────────────────────────────────────────────
    CROW_ROUTE(m_website, "/session/<string>/disconnect").methods("POST"_method)(
        [this](const crow::request&, crow::response& res, std::string name) {
            auto it = m_app.m_sessionMap.find(name);
            if (it != m_app.m_sessionMap.end())
                it->second->getNetwork()->disconnect();
            res.moved("/session/" + name);
            res.end();
        });

    // ── Set Sequence Numbers ─────────────────────────────────────────────────
    CROW_ROUTE(m_website, "/session/<string>/seqnum").methods("POST"_method)(
        [this](const crow::request& req, crow::response& res, std::string name) {
            auto it = m_app.m_sessionMap.find(name);
            if (it != m_app.m_sessionMap.end()) {
                auto params = parseFormBody(req.body);
                auto& session = *it->second;
                auto sit = params.find("sender");
                if (sit != params.end() && !sit->second.empty()) {
                    try { session.setSenderSeqNum(std::stoi(sit->second)); } catch (...) {}
                }
                auto tit = params.find("target");
                if (tit != params.end() && !tit->second.empty()) {
                    try { session.setTargetSeqNum(std::stoi(tit->second)); } catch (...) {}
                }
            }
            res.moved("/session/" + name);
            res.end();
        });

    // ── Message Log ──────────────────────────────────────────────────────────
    CROW_ROUTE(m_website, "/session/<string>/messages")([this](const crow::request& req, crow::response& res, std::string name) {
        auto it = m_app.m_sessionMap.find(name);
        if (it == m_app.m_sessionMap.end()) {
            res.code = 404;
            res.body = notFoundPage(name);
            res.end();
            return;
        }

        auto& session = *it->second;
        std::string enc = htmlEscape(name);

        char* tail_p   = req.url_params.get("tail");
        char* filter_p = req.url_params.get("filter");
        int tail       = tail_p   ? std::atoi(tail_p)   : 200;
        std::string filter = filter_p ? filter_p : "";

        if (tail <= 0 || tail > 5000) tail = 200;

        std::string logPath = getLogBase(session) + ".messages.log";
        auto lines = readLogTail(logPath, tail, filter);

        std::string nav = std::string("<span class=\"breadcrumb\"><a href=\"/\">Dashboard</a> &rsaquo; ")
                        + "<a href=\"/session/" + enc + "\">" + enc + "</a>"
                        + " &rsaquo; Message Log</span>";

        std::ostringstream body;
        body << "<div class=\"page-title\">" << enc << " &ndash; Message Log</div>";

        // Controls
        body << "<form method=\"get\" action=\"/session/" << enc << "/messages\">"
             << "<div class=\"log-controls\">"
             << "<label>Filter: <select name=\"filter\">"
             << "<option value=\"\"" << (filter.empty() ? " selected" : "") << ">All</option>"
             << "<option value=\"SENT\"" << (filter == "SENT" ? " selected" : "") << ">SENT</option>"
             << "<option value=\"RECV\"" << (filter == "RECV" ? " selected" : "") << ">RECV</option>"
             << "</select></label>"
             << "<label>Lines: <select name=\"tail\">"
             << "<option value=\"50\""   << (tail==50   ? " selected" : "") << ">50</option>"
             << "<option value=\"100\""  << (tail==100  ? " selected" : "") << ">100</option>"
             << "<option value=\"200\""  << (tail==200  ? " selected" : "") << ">200</option>"
             << "<option value=\"500\""  << (tail==500  ? " selected" : "") << ">500</option>"
             << "<option value=\"1000\"" << (tail==1000 ? " selected" : "") << ">1000</option>"
             << "</select></label>"
             << "<button class=\"btn btn-blue\" type=\"submit\">Apply</button>"
             << "</div></form>";

        // Log view
        body << "<div class=\"log-view\">";
        if (lines.empty()) {
            body << "<span class=\"empty\">No log entries found.</span>";
        } else {
            for (const auto& line : lines) {
                bool isSent = line.find(" SENT: ") != std::string::npos;
                bool isRecv = line.find(" RECV: ") != std::string::npos;
                std::string cls = isSent ? "log-sent" : (isRecv ? "log-recv" : "");

                body << "<div class=\"log-line " << cls << "\">";
                if (isSent || isRecv) {
                    // Split: "TIMESTAMP SENT: FIXMSG"
                    auto dirPos = line.find(isSent ? " SENT: " : " RECV: ");
                    std::string ts  = line.substr(0, dirPos);
                    std::string msg = line.substr(dirPos + 7);
                    body << "<span style=\"color:#555\">" << htmlEscape(ts) << "</span> "
                         << "<span class=\"dir-badge " << (isSent ? "dir-sent" : "dir-recv") << "\">"
                         << (isSent ? "SENT" : "RECV") << "</span>"
                         << formatFIXMessage(msg);
                } else {
                    body << htmlEscape(line);
                }
                body << "</div>";
            }
        }
        body << "</div>";
        body << "<p style=\"color:#555;font-size:0.75rem;margin-top:8px\">Showing last " << lines.size() << " lines from: " << htmlEscape(logPath) << " &mdash; auto-refreshes every 3s</p>";

        res.body = buildPage(name + " - Messages", nav, body.str(), true, 3000);
        res.end();
    });

    // ── Event Log ────────────────────────────────────────────────────────────
    CROW_ROUTE(m_website, "/session/<string>/events")([this](const crow::request& req, crow::response& res, std::string name) {
        auto it = m_app.m_sessionMap.find(name);
        if (it == m_app.m_sessionMap.end()) {
            res.code = 404;
            res.body = notFoundPage(name);
            res.end();
            return;
        }

        auto& session = *it->second;
        std::string enc = htmlEscape(name);

        char* tail_p = req.url_params.get("tail");
        int tail     = tail_p ? std::atoi(tail_p) : 200;
        if (tail <= 0 || tail > 5000) tail = 200;

        std::string logPath = getLogBase(session) + ".event.log";
        auto lines = readLogTail(logPath, tail, "");

        std::string nav = std::string("<span class=\"breadcrumb\"><a href=\"/\">Dashboard</a> &rsaquo; ")
                        + "<a href=\"/session/" + enc + "\">" + enc + "</a>"
                        + " &rsaquo; Event Log</span>";

        std::ostringstream body;
        body << "<div class=\"page-title\">" << enc << " &ndash; Event Log</div>";

        // Controls
        body << "<form method=\"get\" action=\"/session/" << enc << "/events\">"
             << "<div class=\"log-controls\">"
             << "<label>Lines: <select name=\"tail\">"
             << "<option value=\"50\""   << (tail==50   ? " selected" : "") << ">50</option>"
             << "<option value=\"100\""  << (tail==100  ? " selected" : "") << ">100</option>"
             << "<option value=\"200\""  << (tail==200  ? " selected" : "") << ">200</option>"
             << "<option value=\"500\""  << (tail==500  ? " selected" : "") << ">500</option>"
             << "<option value=\"1000\"" << (tail==1000 ? " selected" : "") << ">1000</option>"
             << "</select></label>"
             << "<button class=\"btn btn-blue\" type=\"submit\">Apply</button>"
             << "</div></form>";

        // Log view
        body << "<div class=\"log-view\">";
        if (lines.empty()) {
            body << "<span class=\"empty\">No log entries found.</span>";
        } else {
            for (const auto& line : lines) {
                body << "<div class=\"log-line log-event\">" << htmlEscape(line) << "</div>";
            }
        }
        body << "</div>";
        body << "<p style=\"color:#555;font-size:0.75rem;margin-top:8px\">Showing last " << lines.size() << " lines from: " << htmlEscape(logPath) << " &mdash; auto-refreshes every 3s</p>";

        res.body = buildPage(name + " - Events", nav, body.str(), true, 3000);
        res.end();
    });

    m_website.loglevel(crow::LogLevel::Warning);

    m_thread = std::thread([this](){
        m_website.port(m_port).run();
    });
}
