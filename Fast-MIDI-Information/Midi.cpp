#include <Windows.h>
#include "Midi.h"
#include <fmt/locale.h>
#include <fmt/format.h>

#pragma region Midi Class

Midi::Midi(wchar_t* file_name)
{
  //Open the file, seek to the end of the file and save that to a var, then return to the start of the file
  file_stream.open(file_name, std::ios::binary | std::ios::ate);
  file_stream.seekg(0, std::ios::end);
  file_end = file_stream.tellg();
  file_stream.seekg(0, std::ios::beg);

  //Load the file
  loadMidi();

  /*
  try
  {
    double seconds = 0;
    uint64_t time = 0;
    while (true) {
      bool all_ended = true;
      for (int i = 0; i < track_count; i++)
      {
        MidiTrack* track = readers[i];
        if (track->ended)
        {
          if (track->time > seconds) seconds = track->time;
          continue;
        }
        all_ended = false;
        if (!track->delta_parsed)
          track->parseDeltaTime();
        if (time < track->tick_time)
          continue;
        while (time >= track->tick_time) {
          track->parseEvent(note_buffer, &misc_events);
          track->parseDeltaTime();
          if (track->ended)
            break;
        }
      }
      time++;
      if (all_ended) break;
    }

    std::cout << "Finished parsing\n";
    std::cout << "Seconds length: " << seconds << "\n";
    uint64_t notes = 0;
    for(int i = 0; i < track_count; i++)
    {
      notes += readers[i]->notes_parsed;
    }
    std::cout << "Notes parsed: " << notes;

  } catch(const char* e)
  {
    std::cout << e;
  }
  */
}

Midi::~Midi()
{
  for(int i = 0; i < 256; i++)
  {
    moodycamel::ReaderWriterQueue<NoteEvent>* notes = note_event_buffer[i];
    delete notes;
  }
  delete note_event_buffer;
}

std::string format_seconds(double secs)
{
    if(secs >= 0)
        return fmt::format("{}:{:04.1f}", (int)floor(secs / 60), fmod(secs, 60));
    else
        return fmt::format("-{}:{:04.1f}", (int)floor(-secs / 60), fmod(-secs, 60));
}

void Midi::loadMidi()
{
  try
  {
    assertText("MThd");
    uint32_t length = parseInt();
    if(length != 6) throw "\nCorrupt Header";

    format = parseShort();
    std::cout << "MIDI Format: " << format << std::endl;
    parseShort();
    ppq = parseShort();

    if(format == 2) throw "\nStop fucking using format 2";

    uint32_t count = 0;

    while(file_stream.tellg() != file_end)
    {
      try
      {
        assertText("MTrk");
        length = parseInt();
        size_t pos = file_stream.tellg();

        if(length + pos > file_end)
        {
          printf("Warning: Track runs past the end of the midi\n");
          length = file_end - pos;
        }

        file_stream.seekg(pos + length, std::ios::beg);

        MidiChunk chunk;
        chunk.start = pos;
        chunk.length = length;
        tracks.push_back(chunk);
        count++;
      } catch(const char* e)
      {
        int track_pos = file_stream.tellg();
        printf("Broken track, not parsing further! pos: %d\n", track_pos);
        break;
      }
    }

    track_count = count;

    //Parse all the tracks and the note cout for each track
    uint64_t nc = 0;
    uint32_t tc = 0;
    uint32_t tn = 0;

    MidiTrack** parse_tracks = new MidiTrack * [track_count];

    concurrency::parallel_for(uint32_t(0), track_count, [&](uint32_t i)
    {
      MidiTrack* track = new MidiTrack(&file_stream, tracks[i].start, tracks[i].length, 100000, i, ppq, &mtx);

      parse_tracks[i] = track;

      while(!track->ended)
      {
        track->parseDelta();
        track->parseEvent(nullptr, nullptr, nullptr);
      }

      mtx.lock();
      tc += track->tempo_events.size();
      tn++;

      std::cout << "\nParsed track: " << fmt::format(std::locale(""), "{:n}", tn) << " (" << fmt::format(std::locale(""), "{:n}", track->notes_parsed) << " notes)";
      nc += (uint64_t)track->notes_parsed;
      mtx.unlock();
    });

    /*for(int i = 0; i < track_count; i++)
    {
      MidiTrack* track = new MidiTrack(&file_stream, tracks[i].start, tracks[i].length, 100000, i, ppq);

      parse_tracks[i] = track;

      while(!track->ended)
      {
        track->parseDelta();
        track->parseEvent1();
      }

      tc += track->tempo_events.size();

      std::cout << "\nParsed track " << i << " note count " << track->notes_parsed;
      nc += (uint64_t)track->notes_parsed;
    }*/

    std::cout << "\nTotal tempo events: " << fmt::format(std::locale(""), "{:n}", tc);
    std::cout << "\nTotal notes: " << fmt::format(std::locale(""), "{:n}", nc);
    std::cout << "\nTotal tracks: " << fmt::format(std::locale(""), "{:n}", tn) << std::endl;

    note_count = nc; //Save the note count to a uint64_t so we can use it later

    tempo_array = new Tempo[tc];
    tempo_count = tc;

    uint32_t t = 0;
    uint32_t* track_tempo_index = new uint32_t[track_count];
    for(int i = 0; i < track_count; i++) track_tempo_index[i] = 0;

    double tick_len = 500.0 / ppq; // 120 bpm
    uint64_t last_tempo_change = 0;

    while(t < tempo_count)
    {
      int min = -1;
      uint64_t min_time = 0;
      for(int i = 0; i < track_count; i++)
      {
        if(track_tempo_index[i] < parse_tracks[i]->tempo_events.size())
        {
          uint64_t pos = parse_tracks[i]->tempo_events[track_tempo_index[i]].pos;
          if(min == -1 || min_time > pos)
          {
            min_time = pos;
            min = i;
          }
        }
      }
      if(min == -1)
      {
        std::cout << "Broke\n";
        break;
      }
      auto tempo = parse_tracks[min]->tempo_events[track_tempo_index[min]];
      tempo_array[t++] = tempo;
      auto tick_delta = tempo.pos - last_tempo_change;
      last_tempo_change = tempo.pos;
      song_len += tick_delta * tick_len;
      tick_len = (double)tempo.tempo / (double)ppq / 1000.0;
      track_tempo_index[min]++;
    }

    uint64_t max_tick = 0;
    for(int i = 0; i < track_count; i++)
    {
      auto track = parse_tracks[i];
      if(track->tick_time > max_tick)
        max_tick = track->tick_time;
    }
    auto tick_delta = max_tick - last_tempo_change;
    song_len += tick_delta * tick_len;

    song_len /= 1000.0;
    //printf("MIDI Length: %f\n", format_seconds(song_len));
    std::cout << "MIDI Length: " << format_seconds(song_len) << " - Ticks: " << max_tick << std::endl;

    uint64_t tick_time = 0;
    for(int i = 0; i < track_count; i++) delete parse_tracks[i];
    delete[] parse_tracks;
    delete[] track_tempo_index;

    readers = new MidiTrack * [count];
    for(int i = 0; i < count; i++)
    {
      readers[i] = new MidiTrack(&file_stream, tracks[i].start, tracks[i].length, 100000, i, ppq, &mtx);
      readers[i]->global_tempo_events = tempo_array;
      readers[i]->global_tempo_event_count = tempo_count;
    }

    note_event_buffer = new moodycamel::ReaderWriterQueue<NoteEvent> * [256];
    for(int i = 0; i < 256; i++)
    {
      note_event_buffer[i] = new moodycamel::ReaderWriterQueue<NoteEvent>();
    }

  } catch(const char* e)
  {
    printf("%s\n", e);
    MessageBoxA(NULL, "This MIDI doesn't appear to be valid.", "Fatal Error", MB_ICONERROR);
    exit(1);
  }
}

void Midi::assertText(const char* text)
{
  for(int i = 0; text[i] != 0; i++)
  {
    if(text[i] != readByte())
    {
      throw "\nCorrupt Chunk Header";
    }
  }
}

uint8_t Midi::readByte()
{
  uint8_t b;
  file_stream.read((char*)&b, 1);

  return b;
}

uint16_t Midi::parseShort()
{
  uint16_t length = 0;
  for(int i = 0; i != 2; i++)
  {
    length = (uint16_t)(length << 8) | (uint8_t)readByte();
  }

  return length;
}

uint32_t Midi::parseInt()
{
  uint32_t length = 0;
  for(int i = 0; i != 4; i++)
  {
    length = (uint32_t)(length << 8) | (uint8_t)readByte();
  }

  return length;
}

void Midi::SpawnLoaderThread()
{
  loader_thread = std::thread(&Midi::LoaderThread, this);
}

void Midi::LoaderThread()
{
  double seconds = -1;
  uint64_t time = 0;
  bool all_ended = false;
  bool* tracks_ended = new bool[track_count];
  while(true)
  {
    memset(tracks_ended, 0, track_count);
    while(seconds < renderer_time.load() + 10000.0f)
    {
      for(int i = 0; i < track_count; i++)
      {
        MidiTrack* track = readers[i];
        if(track->ended)
        {
          tracks_ended[i] = true;
          continue;
        }
        if(!track->delta_parsed)
          track->parseDeltaTime();
        if(time < track->tick_time)
          continue;
        while(time >= track->tick_time)
        {
          track->parseEvent(note_event_buffer, &misc_events, &global_poly);
          if(global_poly > max_global_poly) max_global_poly = global_poly;
          //std::cout << global_poly << std::endl; //Debug
          if(track->time > seconds) seconds = track->time;
          track->parseDeltaTime();
          if(track->ended)
          {
            tracks_ended[i] = true;
            break;
          }
        }
      }
      time++;
      all_ended = true;
      for(int i = 0; i < track_count; i++)
      {
        if(tracks_ended[i] == false)
        {
          all_ended = false;
          break;
        }
      }
      if(all_ended)
        break;
    }
    if(all_ended)
    {
      //printf("\nLoader thread done\n");
      break;
    }
  }
  delete[] tracks_ended;
  //misc_events.enqueue({ static_cast<float>(seconds), PLAYBACK_TERMINATE_EVENT });
  loader_done = true;
  //printf("\nloader thread exiting\n");
}

#pragma endregion

#pragma region BufferedReader class
BufferedReader::BufferedReader(std::ifstream* _file_stream, size_t _start, size_t _length, uint32_t _buffer_size, std::mutex* _mtx)
{
  file_stream = _file_stream;
  start = _start;
  length = _length;
  buffer_size = _buffer_size;
  pos = start;
  mtx = _mtx;
  buffer_start = start;

  if(buffer_size > length) buffer_size = (uint32_t)length;
  buffer = new uint8_t[buffer_size];

  updateBuffer();
}

BufferedReader::~BufferedReader()
{
  delete[] buffer;
}

void BufferedReader::updateBuffer()
{
  uint32_t read = buffer_size;

  if((pos + read) > (start + length))
    read = start + length - pos;

  if(read == 0 && buffer_size != 0)
    throw "\nOutside the buffer";

  mtx->lock();
  file_stream->seekg(pos, std::ios::beg);
  file_stream->read((char*)buffer, read);
  mtx->unlock();
  buffer_start = pos;
  buffer_pos = 0;
}

// origin = fseek origin (SEEK_SET or SEEK_CUR, no need for SEEK_END)
// storage as int64 is fine, nobody will have a midi over 9223 pb
void BufferedReader::seek(int64_t offset, int origin)
{
  if(origin != SEEK_SET && origin != SEEK_CUR)
    throw "Invalid seek origin!";

  int64_t real_offset = offset;
  if(origin == SEEK_SET)
    real_offset += start;
  else
    real_offset += pos;

  if(real_offset < (int64_t)start)
    throw "Attempted to seek before start!";
  if(real_offset > start + length)
    throw "Attempted to seek past end!";

  pos = real_offset;

  // buffer doesn't have to be remade if seeking between it already
  if(buffer_start <= real_offset && real_offset < buffer_start + buffer_size)
  {
    buffer_pos = pos - buffer_start;
    return;
  }

  updateBuffer();
}

void BufferedReader::read(uint8_t* dst, size_t size)
{
  if(pos + size > start + length)
    throw "Attempted to read past end!";
  if(size > buffer_size)
    throw "(UMIMPLEMENTED) Requested read size is larger than the buffer size!";

  if(buffer_start + buffer_pos + size > buffer_start + buffer_size)
    updateBuffer();

  memcpy(dst, buffer + buffer_pos, size);
  pos += size;
  buffer_pos += size;
}

uint8_t BufferedReader::readByte()
{
  uint8_t ret;
  read(&ret, 1);
  return ret;
}

void BufferedReader::skipBytes(size_t size)
{
  seek(size, SEEK_CUR);
}

#pragma endregion

#pragma region Midi Track

MidiTrack::MidiTrack(std::ifstream* _file_stream, size_t _start, size_t _length, uint32_t _buffer_size, uint32_t _track_num, uint16_t _ppq, std::mutex* _mtx)
{
  reader = new BufferedReader(_file_stream, _start, _length, _buffer_size, _mtx);
  ppq = _ppq;
  track_num = _track_num;
  tempo_multiplier = multiplierFromTempo(500000, ppq);
}

MidiTrack::~MidiTrack()
{
  delete reader;
}

void MidiTrack::parseDelta()
{
  if(ended) return;
  try
  {
    uint8_t c;
    uint32_t val = 0;
    while((c = reader->readByte()) > 0x7F)
    {
      val = (val << 7) | (c & 0x7F);
    }
    val = val << 7 | c;

    tick_time += val;
    delta_parsed = true;
  } catch(const char* e)
  {
    ended = true;
  }
}

void MidiTrack::parseDeltaTime()
{
  if(ended) return;
  try
  {
    uint8_t c;
    uint32_t val = 0;
    while((c = reader->readByte()) > 0x7F)
    {
      val = (val << 7) | (c & 0x7F);
    }
    val = val << 7 | c;

    while(global_tempo_event_index < global_tempo_event_count && val + tick_time > global_tempo_events[global_tempo_event_index].pos)
    {
      Tempo t = global_tempo_events[global_tempo_event_index];

      uint64_t offset = t.pos - tick_time;
      val -= offset;
      time += offset * tempo_multiplier;
      tempo_multiplier = multiplierFromTempo(t.tempo, ppq);
      tick_time += offset;

      global_tempo_event_index++;
    }

    time += val * tempo_multiplier;
    tick_time += val;
    delta_parsed = true;
  } catch(const char* e)
  {
    ended = true;
  }
}

double MidiTrack::multiplierFromTempo(uint32_t tempo, uint16_t ppq)
{
  return tempo / 1000000.0 / ppq;
}

void MidiTrack::parseEvent(moodycamel::ReaderWriterQueue<NoteEvent>** global_note_events, moodycamel::ReaderWriterQueue<MidiEvent>* global_misc, uint64_t* global_poly)
{
  bool stage_2 = global_note_events != nullptr;
  if(ended)
  {
    return;
  }
  try
  {
    delta_parsed = false;
    uint8_t command = reader->readByte();
    if(command < 0x80)
    {
      reader->seek(-1, SEEK_CUR);
      command = prev_command;
    }

    prev_command = command;

    uint8_t cmd = (uint8_t)(command & 0xF0);
    uint8_t channel = (uint8_t)(command & 0x0F);

    switch(cmd)
    {
      case 0x80: // Note Off
        if(stage_2)
        {
          reader->readByte();
          reader->readByte();
          if((*global_poly) != 0) (*global_poly)--;
          break;
        }
        else
        {
          reader->readByte();
          reader->readByte();
        }
        break;
      case 0x90: // Note On
        if(stage_2)
        {
          reader->readByte();
          uint8_t vel = reader->readByte();

          if(vel > 0)
          {
            (*global_poly)++;
          }
          else
          {
            if((*global_poly) != 0) (*global_poly)--;
          }
          break;
        }
        else
        {
          reader->readByte();
          if(reader->readByte() > 0) notes_parsed++;
        }
        break;
      case 0xA0: // Polyphonic Pressure
      case 0xB0: // Controller
      case 0xE0: // Pitch Wheel
        reader->skipBytes(2);
        break;
      case 0xC0: // Program Change
      case 0xD0: // Channel Pressure
        reader->skipBytes(1);
        break;
      case 0xF0: // System Event
      {
        switch(command)
        {
          case 0xFF:
          {
            uint8_t command2 = reader->readByte();

            uint8_t c;
            uint32_t val = 0;
            while((c = reader->readByte()) > 0x7F)
            {
              val = (val << 7) | (c & 0x7F);
            }
            val = val << 7 | c;

            switch(command2)
            {
              case 0x00: // Sequence number
                reader->skipBytes(2);
                break;
              case 0x01: // Text
              case 0x02: // Copyright info
              case 0x03: // Track name
              case 0x04: // Track instrument name
              case 0x05: // Lyric
              case 0x06: // Marker
              case 0x07: // Cue point
              case 0x0A:
              {
                if(stage_2)
                {
                  uint8_t* data = new uint8_t[val + 1];
                  for(int i = 0; i < val; i++) data[i] = reader->readByte();
                  if(command2 == 0x06)
                  {
                    //TODO: Actually do something with this please
                    data[val] = '\0';
                    std::cout << "(" << format_seconds(time) << ") Marker Event: " << data << std::endl;
                  }
                  if(command2 == 0x0A && (val == 8 || val == 12) && data[0] == 0x00 && data[1] == 0x0F && (data[2] < 16 || data[2] == 0x7F) && data[3] == 0)
                  {
                    if(val == 8)
                    {
                      //double delta, byte channel, byte r, byte g, byte b, byte a
                      delete[] data;
                      break;
                    }
                    //double delta, byte r, byte g, byte b, byte a, byte r2, byte g2, byte b2, byte a2
                    delete[] data;
                    break;
                  }
                  delete[] data;
                  break;
                }
                else {
                  reader->skipBytes(val);
                  break;
                }
              }
              case 0x7F: // Sequencer-specific information
                reader->skipBytes(val);
                break;
              case 0x20: // MIDI Channel prefix
                reader->skipBytes(1);
                break;
              case 0x21: // MIDI Port
                reader->skipBytes(1);
                break;
              case 0x2F:
                // End of track
                ended = true;
                break;
              case 0x51: // Tempo
              {
                uint32_t tempo = 0;
                for(int i = 0; i != 3; i++)
                  tempo = (uint32_t)((tempo << 8) | reader->readByte());

                if(!stage_2)
                {
                  Tempo t;
                  t.pos = tick_time;
                  t.tempo = tempo;
                  tempo_events.push_back(t);
                }
                break;
              }
              case 0x54:
                // SMPTE Offset
                reader->skipBytes(5);
                break;
              case 0x58:
                // Time signature
                reader->skipBytes(4);
                break;
              case 0x59:
                // Key signature
                reader->skipBytes(2);
                break;

              default:
              {
                printf("\nUnknown system event 0x%x", command2);
                reader->skipBytes(val);
                break;
              }
            }
            break;
          }
          case 0xF0:
            while(reader->readByte() != 0xF7);
            break;
          case 0xF2:
            reader->readByte();
            reader->readByte();
            break;
          case 0xF3:
            reader->readByte();
            break;
          default:
            break;
        }
        break;
      }
    }
  } catch(const char* e)
  {
    std::cout << e;
    ended = true;
  }
}

#pragma endregion
