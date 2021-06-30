#include <iostream>
#include <filesystem>
#include <codecvt>
#include <string>
#include <Windows.h>
#include "Midi.h"
#include <fmt/locale.h>
#include <fmt/format.h>

Midi* midi;

std::wstring GetFileName(std::filesystem::path file_path)
{
    return std::filesystem::path(file_path).filename().wstring();
}

std::string wstringToUtf8(std::wstring str)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
    return conv.to_bytes(str);
}

std::wstring OpenMIDIFileDialog()
{
    OPENFILENAMEW ofn = { 0 };
    wchar_t file_name[1024] = { 0 };
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.lpstrFilter = L"MIDI Files\0*.mid\0";
    ofn.lpstrFile = file_name;
    ofn.nMaxFile = sizeof(file_name) / sizeof(wchar_t);
    ofn.lpstrTitle = L"Open a MIDI";
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    GetOpenFileNameW(&ofn);
    return std::wstring(file_name);
}

std::string format_seconds(float secs)
{
  if(secs >= 0)
    return fmt::format("{}:{:04.1f}", (int)floor(secs / 60), fmod(secs, 60));
  else
    return fmt::format("-{}:{:04.1f}", (int)floor(-secs / 60), fmod(-secs, 60));
}

int wmain(int argc, wchar_t** argv)
{
    std::wstring filename;
    if(argc < 2)
    {
        filename = OpenMIDIFileDialog();
        if(filename.empty())
            return EXIT_FAILURE;
    }
    else
    {
        filename = argv[1];
    }

    fmt::print("Loading {}\n", wstringToUtf8(GetFileName(filename)));
    wchar_t* filename_temp = _wcsdup(filename.c_str());
    static auto parse_start = std::chrono::high_resolution_clock::now();
    midi = new Midi(filename_temp);
    midi->SpawnLoaderThread();
    while(!midi->loader_done)
    {
      auto current_time = std::chrono::high_resolution_clock::now();
      float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - parse_start).count();
      midi->renderer_time.store(time);
    }

    static auto parse_end = std::chrono::high_resolution_clock::now();
    std::cout << "\nMax Polyphony: " << fmt::format(std::locale(""), "{:n}", midi->max_global_poly) << std::endl;
    std::cout << "\nParsing took: " << format_seconds(std::chrono::duration<float, std::chrono::seconds::period>(parse_end - parse_start).count()) << std::endl;
    system("pause");
    return EXIT_SUCCESS;
}
