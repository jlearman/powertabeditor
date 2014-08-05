/*
  * Copyright (C) 2011 Cameron White
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * (at your option) any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef FORMATS_GUITARPROIMPORTER_H
#define FORMATS_GUITARPROIMPORTER_H

#include <formats/fileformat.h>

namespace Gp
{
    struct Beat;
    struct Document;
    struct Header;
    struct Measure;
}

class KeySignature;
class ScoreInfo;
class System;
class TimeSignature;
class Voice;

/// Imports Guitar Pro files.
class GuitarProImporter : public FileFormatImporter
{
public:
    GuitarProImporter();

    virtual void load(const std::string &filename, Score &score) override;

private:
    static void convertHeader(const Gp::Header &header, ScoreInfo &info);
    static void convertPlayers(const Gp::Document &doc, Score &score);
    static int convertBarline(const Gp::Measure &measure,
                              const Gp::Measure *prevMeasure, System &system,
                              int start, int end, KeySignature &lastKeySig,
                              TimeSignature &lastTimeSig);
    static void convertAlternateEndings(const Gp::Measure &measure,
                                        System &system, int position);
    static int convertBeat(const Gp::Beat &beat, System &system, Voice &voice,
                           int position);
    static void convertScore(const Gp::Document &doc, Score &score);

#if 0
    /// Reads a beat (Guitar Pro equivalent of a Position in Power Tab).
    void readBeat(Gp::InputStream &stream, const Tuning &tuning, Position &pos,
                  boost::optional<TempoMarker> &tempoMarker);

    /// Reads a duration value and converts it into PTB format.
    Position::DurationType readDuration(Gp::InputStream &stream);

    void readChordDiagram(Gp::InputStream &stream, const Tuning &tuning);

    /// Reads an old-style "simple" chord from earlier versions.
    void readOldStyleChord(Gp::InputStream &stream, const Tuning &tuning);

    void readPositionEffects(Gp::InputStream &stream, Position &position);

    void readTremoloBar(Gp::InputStream &stream, Position &position);

    void readMixTableChangeEvent(Gp::InputStream &stream,
                                 boost::optional<TempoMarker> &tempoMarker);

#if 0
    static uint8_t convertTremoloEventType(uint8_t gpEventType);
    static uint8_t convertBendPitch(int32_t gpBendPitch);
#endif

    void readNotes(Gp::InputStream &stream, Position &position);

    void readNoteEffects(Gp::InputStream &stream, Position &position,
                         Note &note);
    void readNoteEffectsGp3(Gp::InputStream &stream, Position &position,
                            Note &note);

    void readSlide(Gp::InputStream &stream, Note &note);
    void readHarmonic(Gp::InputStream &stream, Note &note);
    void readBend(Gp::InputStream &stream, Note &note);
#endif
};

#endif
