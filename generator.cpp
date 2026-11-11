#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <random>
#include <filesystem>
#include <cctype>
#include <algorithm>
#include <fstream>

namespace fs = std::filesystem;

std::vector<std::string> words;

std::mt19937 rng(std::random_device{}());

int randIndex() {
    static std::uniform_int_distribution<int> dist(0, (int)words.size() - 1);
    return dist(rng);
}

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start])) ++start;
    size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end - 1])) --end;
    return s.substr(start, end - start);
}

bool loadWordList(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    std::vector<std::string> loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string w = trim(line);
        if (!w.empty()) loaded.push_back(w);
    }
    if (loaded.empty()) return false;

    words = std::move(loaded);
    return true;
}

void loadDefaultWords() {
    words = {
        "alpha","bravo","charlie","delta","echo","foxtrot","golf","hotel","india","juliett",
        "kilo","lima","mike","nov","oscar","papa","quebec","romeo","sierra","tango",
        "uniform","victor","whiskey","xray","yankee","zulu"
    };
}

std::string randomWord() { return words[randIndex()]; }

std::string randomSentence() {
    int n = 5 + (randIndex() % 6);
    std::string s;
    for (int i = 0; i < n; ++i) {
        if (i) s += " ";
        s += words[randIndex()];
    }
    s += ".";
    return s;
}

std::string randomParagraph() {
    int sentences = 3 + (randIndex() % 4);
    std::string p;
    for (int i = 0; i < sentences; ++i) {
        if (i) p += " ";
        p += randomSentence();
    }
    return p;
}

std::string escapePdf(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '(' || c == ')') out += '\\';
        out += c;
    }
    return out;
}

std::vector<std::string> wrapParasToLines(const std::vector<std::string>& paras, int& totalChars) {
    std::vector<std::string> lines;
    totalChars = 0;
    for (const auto& p : paras) {
        totalChars += (int)p.size();
        std::vector<std::string> w;
        std::string cur;
        for (char c : p) {
            if (isspace((unsigned char)c)) {
                if (!cur.empty()) { w.push_back(cur); cur.clear(); }
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) w.push_back(cur);

        std::string line;
        for (const auto& word : w) {
            if (!line.empty() && (int)(line.size() + word.size() + 1) > 90) {
                lines.push_back(line);
                line.clear();
            }
            if (!line.empty()) line += " ";
            line += word;
        }
        if (!line.empty()) lines.push_back(line);
        lines.push_back(""); // paragraph break
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

std::string buildPdf(const std::string& title, const std::vector<std::string>& paras, int& pageCount, int& totalChars) {
    std::vector<std::string> lines = wrapParasToLines(paras, totalChars);

    const int linesPerPage = 45;
    int numPages = ((int)lines.size() + linesPerPage - 1) / linesPerPage;
    if (numPages < 1) numPages = 1;
    pageCount = numPages;

    std::vector<std::string> bodies;
    bodies.push_back("<< /Type /Catalog /Pages 2 0 R >>");
    bodies.push_back("<< /Type /Pages /Kids [ ] /Count 0 >>");
    bodies.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");

    std::vector<int> pageNums;
    int nextNum = 4;
    for (int i = 0; i < numPages; ++i) {
        int cnum = nextNum++;
        std::string content;
        int start = i * linesPerPage;
        int end = std::min((int)lines.size(), start + linesPerPage);
        int y = 777;

        if (i == 0 && !title.empty()) {
            content += "BT /F1 16 Tf 50 777 Td (" + escapePdf(title) + ") Tj ET\n";
            y = 750;
        }
        for (int j = start; j < end; ++j) {
            if (lines[j].empty()) { y -= 14; continue; }
            content += "BT /F1 12 Tf 50 " + std::to_string(y) + " Td (" + escapePdf(lines[j]) + ") Tj ET\n";
            y -= 14;
        }

        std::string contentBody = "<< /Length " + std::to_string((int)content.size()) + " >>\nstream\n" + content + "endstream";
        bodies.push_back(contentBody);

        int pnum = nextNum++;
        pageNums.push_back(pnum);
        std::string pageBody = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 3 0 R >> >> /Contents " + std::to_string(cnum) + " 0 R >>";
        bodies.push_back(pageBody);
    }

    std::string kids;
    for (int i = 0; i < numPages; ++i) {
        kids += std::to_string(pageNums[i]) + " 0 R";
        if (i < numPages - 1) kids += " ";
    }
    bodies[1] = "<< /Type /Pages /Kids [ " + kids + " ] /Count " + std::to_string(numPages) + " >>";

    std::string pdf = "%PDF-1.4\n";
    std::vector<int> offsets(bodies.size() + 1, 0);
    for (size_t i = 0; i < bodies.size(); ++i) {
        int num = (int)i + 1;
        offsets[num] = (int)pdf.size();
        pdf += std::to_string(num) + " 0 obj\n" + bodies[i] + "\nendobj\n";
    }

    int xrefPos = (int)pdf.size();
    int objCount = (int)bodies.size();
    pdf += "xref\n0 " + std::to_string(objCount + 1) + "\n";
    pdf += "0000000000 65535 f \n";
    char xrefEntry[32];
    for (int i = 1; i <= objCount; ++i) {
        std::snprintf(xrefEntry, sizeof(xrefEntry), "%010d 00000 n \n", offsets[i]);
        pdf += xrefEntry;
    }
    pdf += "trailer\n<< /Size " + std::to_string(objCount + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xrefPos) + "\n%%EOF";

    return pdf;
}

int main(int argc, char* argv[]) {
    // Try to load dictionary from words.txt (argument, cwd, or exe directory) with built-in fallback.
    bool loaded = false;
    std::string loadedFrom;

    if (argc > 1) {
        fs::path p = fs::u8path(argv[1]);
        if (loadWordList(p)) { loaded = true; loadedFrom = p.string(); }
    }
    if (!loaded) {
        fs::path p = "words.txt";
        if (loadWordList(p)) { loaded = true; loadedFrom = fs::absolute(p).string(); }
    }
    if (!loaded && argc > 0) {
        fs::path exeDir = fs::path(fs::u8path(argv[0])).parent_path();
        if (!exeDir.empty()) {
            fs::path p = exeDir / "words.txt";
            if (loadWordList(p)) { loaded = true; loadedFrom = p.string(); }
        }
    }

    if (!loaded) {
        loadDefaultWords();
        std::printf("Using built-in word list (%zu words). Place 'words.txt' next to the exe or in the current directory to use an external dictionary.\n", words.size());
    } else {
        std::printf("Loaded %zu words from '%s'.\n", words.size(), loadedFrom.c_str());
    }

    const char* userProfile = std::getenv("USERPROFILE");
    std::string saveDir = userProfile != nullptr ? std::string(userProfile) + "/Documents/Fake_Books" : "C:\\Users\\Zemo\\Documents\\Fake_Books";

    std::error_code ec;
    fs::create_directories(saveDir, ec);
    if (ec) {
        std::fprintf(stderr, "Failed to create directory: %s\n", saveDir.c_str());
        return 1;
    }

    for (const auto& entry : fs::directory_iterator(saveDir, ec)) {
        if (ec) break;
        if (entry.is_regular_file(ec) && entry.path().extension() == ".pdf") {
            fs::remove(entry.path(), ec);
        }
    }

    for (int i = 0; i < 5; ++i) {
        std::vector<std::string> paras;
        int totalChars = 0;
        int pageCount = 0;
        const int linesPerPage = 45;

        // Generate enough content to satisfy >=2700 chars and >=10 pages (no blank padding)
        while (true) {
            paras.push_back(randomParagraph());
            std::vector<std::string> lines = wrapParasToLines(paras, totalChars);
            pageCount = ((int)lines.size() + linesPerPage - 1) / linesPerPage;
            if (pageCount < 1) pageCount = 1;
            if (totalChars >= 2700 && pageCount >= 10) break;
        }

        std::string title = randomSentence();
        std::string pdf = buildPdf(title, paras, pageCount, totalChars);

        std::string name = randomWord() + "_" + randomWord() + ".pdf";
        std::string path = saveDir + "/" + name;

        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            std::fprintf(stderr, "Failed to open file for writing: %s\n", path.c_str());
            continue;
        }
        size_t written = std::fwrite(pdf.data(), 1, pdf.size(), f);
        std::fclose(f);
        if (written != pdf.size()) {
            std::fprintf(stderr, "Failed to write complete file: %s\n", path.c_str());
            continue;
        }

        std::printf("[+] Fayl yaradildi: %s\n", path.c_str());
    }

    std::printf("\nAll files created successfully!\n");
    return 0;
}