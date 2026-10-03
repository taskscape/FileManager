// SPDX-FileCopyrightText: 2026 Taskscape Ltd
// SPDX-License-Identifier: GPL-2.0-or-later

#include "report.h"

namespace reorg
{
namespace
{

std::string CsvCell(const std::wstring& value)
{
    std::string text = WideToUtf8(value);
    if (!text.empty())
    {
        char ch = text[0];
        if (ch == '=' || ch == '+' || ch == '-' || ch == '@' || ch == '\t' || ch == '\r')
            text.insert(text.begin(), '\'');
    }
    std::string out = "\"";
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '"')
            out += "\"\"";
        else
            out.push_back(text[i]);
    }
    out.push_back('"');
    return out;
}

std::string HtmlEscape(const std::wstring& value)
{
    std::string text = WideToUtf8(value);
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '&')
            out += "&amp;";
        else if (text[i] == '<')
            out += "&lt;";
        else if (text[i] == '>')
            out += "&gt;";
        else if (text[i] == '"')
            out += "&quot;";
        else
            out.push_back(text[i]);
    }
    return out;
}

} // namespace

std::string WriteReportCsv(const std::vector<CReportRow>& rows)
{
    std::string out = "\xEF\xBB\xBF#,Kind,Original path,Proposed path,Reason,Reversible,Issue codes,Issue messages,Resolution,Acknowledged\r\n";
    for (size_t i = 0; i < rows.size(); ++i)
    {
        char index[32];
        _snprintf_s(index, _countof(index), _TRUNCATE, "%u", (unsigned)(i + 1));
        out += index;
        out += ",";
        out += CsvCell(rows[i].Kind);
        out += ",";
        out += CsvCell(rows[i].Original);
        out += ",";
        out += CsvCell(rows[i].Proposed);
        out += ",";
        out += CsvCell(rows[i].Reason);
        out += ",";
        out += CsvCell(rows[i].Reversible);
        out += ",";
        out += CsvCell(rows[i].IssueCodes);
        out += ",";
        out += CsvCell(rows[i].IssueMessages);
        out += ",";
        out += CsvCell(rows[i].Resolution);
        out += ",";
        out += CsvCell(rows[i].Acknowledged);
        out += "\r\n";
    }
    return out;
}

std::string WriteReportHtml(const std::wstring& planName, const std::wstring& planId, const std::wstring& hash, const std::vector<CReportRow>& rows)
{
    std::string html = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>";
    html += HtmlEscape(planName);
    html += "</title><style>body{font-family:Segoe UI,sans-serif}table{border-collapse:collapse}td,th{border:1px solid #ccc;padding:4px}</style></head><body><h1>";
    html += HtmlEscape(planName);
    html += "</h1><p>";
    html += HtmlEscape(planId);
    html += " ";
    html += HtmlEscape(hash);
    html += "</p><table><tr><th>#</th><th>Kind</th><th>Original path</th><th>Proposed path</th><th>Reason</th><th>Reversible</th></tr>";
    for (size_t i = 0; i < rows.size(); ++i)
    {
        char index[32];
        _snprintf_s(index, _countof(index), _TRUNCATE, "%u", (unsigned)(i + 1));
        html += "<tr><td>";
        html += index;
        html += "</td><td>";
        html += HtmlEscape(rows[i].Kind);
        html += "</td><td>";
        html += HtmlEscape(rows[i].Original);
        html += "</td><td>";
        html += HtmlEscape(rows[i].Proposed);
        html += "</td><td>";
        html += HtmlEscape(rows[i].Reason);
        html += "</td><td>";
        html += HtmlEscape(rows[i].Reversible);
        html += "</td></tr>";
    }
    html += "</table></body></html>";
    return html;
}

} // namespace reorg
