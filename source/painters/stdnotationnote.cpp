/*
  * Copyright (C) 2013 Cameron White
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

#include "stdnotationnote.h"

#include <boost/algorithm/string/predicate.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <boost/unordered_map.hpp>
#include <map>
#include <numeric>
#include <painters/layoutinfo.h>
#include <painters/musicfont.h>
#include <QFontMetricsF>
#include <score/generalmidi.h>
#include <score/score.h>
#include <score/staffutils.h>
#include <score/tuning.h>

/// Maps notes to their position on the staff (relative to the top line),
/// using units of 0.5 * STD_NOTATION_LINE_SPACING.
/// This is for treble clef, but can easily be adjusted for bass clef.
static const boost::unordered_map<char, int> theNotePositions =
        boost::assign::map_list_of('F', 0) ('E', 1) ('D', 2) ('C', 3) ('B', -3)
                                  ('A', -2) ('G', -1);

StdNotationNote::StdNotationNote(const Position &pos, const Note &note,
                                 const KeySignature &key, const Tuning &tuning,
                                 double y)
    : myY(y),
      myAccidentalType(NoAccidental),
      myPosition(&pos),
      myNote(&note),
      myKey(&key),
      myTuning(&tuning)
{
    // Choose the note head symbol.
    switch (pos.getDurationType())
    {
    case Position::WholeNote:
        myNoteHeadSymbol = MusicFont::WholeNote;
        break;
    case Position::HalfNote:
        myNoteHeadSymbol = MusicFont::HalfNote;
        break;
    default:
        myNoteHeadSymbol = MusicFont::QuarterNoteOrLess;
        break;
    }

    if (note.hasProperty(Note::NaturalHarmonic))
        myNoteHeadSymbol = MusicFont::NaturalHarmonicNoteHead;
    // TODO - handle artificial harmonics.
    else if (note.hasTappedHarmonic())
        myNoteHeadSymbol = MusicFont::ArtificialHarmonicNoteHead;
    else if (note.hasProperty(Note::Muted))
        myNoteHeadSymbol = MusicFont::MutedNoteHead;

    computeAccidentalType(false);
}

void StdNotationNote::getNotesInStaff(const Score &score, const System &system,
                                      int systemIndex, const Staff &staff,
                                      int staffIndex, const LayoutInfo &layout,
                                      std::vector<StdNotationNote> &notes,
                                      std::vector<BeamGroup> &groups)
{
    // If there is no active player, use standard 8-string tuning as a default
    // for calculating the music notation.
    Tuning fallbackTuning;
    std::vector<uint8_t> tuningNotes = fallbackTuning.getNotes();
    tuningNotes.push_back(Midi::MIDI_NOTE_B2);
    tuningNotes.push_back(Midi::MIDI_NOTE_E1);
    fallbackTuning.setNotes(tuningNotes);

    QFont musicFont(MusicFont().getFont());
    QFontMetricsF fm(musicFont);

    for (int voice = 0; voice < Staff::NUM_VOICES; ++voice)
    {
        BOOST_FOREACH(const Barline &bar, system.getBarlines())
        {
            const Barline *nextBar = system.getNextBarline(bar.getPosition());
            if (!nextBar)
                break;

            std::vector<NoteStem> stems;

            // Store the current accidental for each line/space in the staff.
            std::map<int, AccidentalType> accidentals;

            BOOST_FOREACH(const Position &pos, StaffUtils::getPositionsInRange(
                              staff, voice, bar.getPosition(),
                              nextBar->getPosition()))
            {
                Q_ASSERT(pos.getPosition() == 0 ||
                         pos.getPosition() != bar.getPosition());
                Q_ASSERT(pos.getPosition() == 0 ||
                         pos.getPosition() != nextBar->getPosition());

                std::vector<double> noteLocations;

                if (pos.isRest() || pos.hasMultiBarRest())
                {
                    stems.push_back(NoteStem(pos, 0, 0, noteLocations));
                    continue;
                }

                // Find an active player so that we know what tuning to use.
                std::vector<ActivePlayer> activePlayers;
                const PlayerChange *players = ScoreUtils::getCurrentPlayers(
                            score, systemIndex, pos.getPosition());
                if (players)
                    activePlayers = players->getActivePlayers(staffIndex);

                const Player *player = 0;
                if (!activePlayers.empty())
                {
                    player = &score.getPlayers()[
                            activePlayers.front().getPlayerNumber()];
                }

                double noteHeadWidth = 0;

                BOOST_FOREACH(const Note &note, pos.getNotes())
                {
                    const Tuning tuning = player ? player->getTuning() :
                                                   fallbackTuning;
                    const double y = getNoteLocation(
                                staff, note, bar.getKeySignature(), tuning);

                    noteLocations.push_back(y);

                    notes.push_back(StdNotationNote(
                                        pos, note, bar.getKeySignature(),
                                        tuning, y));
                    StdNotationNote &stdNote = notes.back();

                    // Don't show accidentals if there are consecutive
                    // identical notes on that line/space in the staff.
                    if (accidentals.find(y) != accidentals.end() &&
                        accidentals.find(y)->second == stdNote.getAccidentalType())
                    {
                        stdNote.clearAccidental();
                    }
                    else
                    {
                        AccidentalType accidental = stdNote.getAccidentalType();
                        // If we had some accidental and then returned to a note
                        // in the key signature, then force its accidental or
                        // natural sign to be shown.
                        if (accidentals.find(y) != accidentals.end() &&
                            accidental == NoAccidental)
                        {
                            stdNote.showAccidental();
                        }

                        accidentals[y] = accidental;
                    }

                    noteHeadWidth = fm.width(stdNote.getNoteHeadSymbol());
                }

                const double x = layout.getPositionX(pos.getPosition()) +
                        0.5 * (layout.getPositionSpacing() - noteHeadWidth);
                stems.push_back(NoteStem(pos, x, noteHeadWidth, noteLocations));
            }

            computeBeaming(bar.getTimeSignature(), stems, groups);
        }
    }
}

double StdNotationNote::getY() const
{
    return myY;
}

double StdNotationNote::getNoteLocation(const Staff &staff, const Note &note,
                                        const KeySignature &key,
                                        const Tuning &tuning)
{
    const int pitch = tuning.getNote(note.getString(), true) + note.getFretNumber();

    const std::string text = Midi::getMidiNoteText(
                pitch, key.getKeyType() == KeySignature::Minor,
                key.usesSharps() || key.getNumAccidentals() == 0,
                key.getNumAccidentals());

    // Find the offset of the note - accidentals don't matter, so C# and C
    // have the same offset, for example.
    int y = theNotePositions.find(text[0])->second;

    // Adjust for bass clef, where A is the note at the top line.
    if (staff.getClefType() == Staff::BassClef)
        y += 2;

    const int topNote = (staff.getClefType() == Staff::TrebleClef) ?
                Midi::MIDI_NOTE_F4 : Midi::MIDI_NOTE_A2;

    // Shift the note by the number of octaves.
    y += 7 * (Midi::getMidiNoteOctave(topNote) -
              Midi::getMidiNoteOctave(pitch, text[0])) +
            7 * getOctaveOffset(note);

    return y * 0.5 * LayoutInfo::STD_NOTATION_LINE_SPACING;
}

int StdNotationNote::getOctaveOffset(const Note &note)
{
    if (note.hasProperty(Note::Octave8va))
        return 1;
    else if (note.hasProperty(Note::Octave15ma))
        return 2;
    else if (note.hasProperty(Note::Octave8vb))
        return -1;
    else if (note.hasProperty(Note::Octave15mb))
        return -2;
    else
        return 0;
}

void StdNotationNote::computeAccidentalType(bool explicitSymbol)
{
    using boost::algorithm::ends_with;

    const int pitch = myTuning->getNote(myNote->getString(), true) +
            myNote->getFretNumber();
    const bool usesSharps = myKey->usesSharps() || myKey->getNumAccidentals() == 0;

    const std::string noteText = Midi::getMidiNoteText(
                pitch, myKey->getKeyType() == KeySignature::Minor, usesSharps,
                myKey->getNumAccidentals(), explicitSymbol);

    if (ends_with(noteText, "##"))
        myAccidentalType = DoubleSharp;
    else if (ends_with(noteText, "#"))
        myAccidentalType = Sharp;
    else if (ends_with(noteText, "bb"))
        myAccidentalType = DoubleFlat;
    else if (ends_with(noteText, "b"))
        myAccidentalType = Flat;
    else if (ends_with(noteText, "="))
        myAccidentalType = Natural;
    else
        myAccidentalType = NoAccidental;
}

std::vector<uint8_t> StdNotationNote::getBeamingPatterns(
        const TimeSignature &timeSig)
{
    TimeSignature::BeamingPattern pattern(timeSig.getBeamingPattern());
    std::vector<uint8_t> beaming(pattern.begin(), pattern.end());
    beaming.erase(std::remove(beaming.begin(), beaming.end(), 0),
                  beaming.end());
    return beaming;
}

void StdNotationNote::computeBeaming(const TimeSignature &timeSig,
                                     const std::vector<NoteStem> &stems,
                                     std::vector<BeamGroup> &groups)
{
    const std::vector<uint8_t> beamingPatterns(getBeamingPatterns(timeSig));

    // Create a list of the durations for each stem.
    std::vector<double> durations(stems.size());
    std::transform(stems.begin(), stems.end(), durations.begin(),
                   std::mem_fun_ref(&NoteStem::getDurationTime));
    // Convert the duration list to a list of timestamps relative to the
    // beginning of the bar.
    std::partial_sum(durations.begin(), durations.end(), durations.begin());

    double groupBeginTime = 0;
    std::vector<uint8_t>::const_iterator groupSize = beamingPatterns.begin();
    std::vector<double>::iterator groupStart = durations.begin();
    std::vector<double>::iterator groupEnd = durations.begin();

    while (groupEnd != durations.end())
    {
        // Find the timestamp where the end of the current pattern group will be.
        const double groupEndTime = *groupSize * 0.5 + groupBeginTime;

        // Get the stems in the pattern group.
        groupStart = std::lower_bound(groupEnd, durations.end(), groupBeginTime);
        groupEnd = std::upper_bound(groupStart, durations.end(), groupEndTime);

        std::vector<NoteStem> patternGroup(
                    stems.begin() + (groupStart - durations.begin()),
                    stems.begin() + (groupEnd - durations.begin()));

        computeBeamingGroups(patternGroup, groups);

        // Move on to the next beaming pattern, looping around if necessary.
        ++groupSize;
        if (groupSize == beamingPatterns.end())
            groupSize = beamingPatterns.begin();

        groupBeginTime = groupEndTime;
    }
}

void StdNotationNote::computeBeamingGroups(const std::vector<NoteStem> &stems,
                                           std::vector<BeamGroup> &groups)
{
    // Rests and notes greater than eighth notes will break apart a beam group,
    // so we need to find all of the subgroups of consecutive positions that
    // can be beamed, and then create beaming groups with those notes.
    std::vector<NoteStem>::const_iterator beamableGroupStart = stems.begin();
    std::vector<NoteStem>::const_iterator beamableGroupEnd = stems.begin();

    // Find all subgroups of beamable notes (i.e. consecutive notes that aren't
    // quarter notes, rests, etc).
    while (beamableGroupEnd != stems.end())
    {
        // Find the next range of consecutive stems that are beamable.
        beamableGroupStart = std::find_if(beamableGroupEnd, stems.end(),
                                          &NoteStem::isBeamable);

        // If there were any notes that aren't beamed but need stems (like a
        // half note or a quarter note), create a single-item beam group for
        // each.
        for (std::vector<NoteStem>::const_iterator it = beamableGroupEnd;
             it != beamableGroupStart; ++it)
        {
            if (NoteStem::needsStem(*it))
            {
                groups.push_back(BeamGroup(std::vector<NoteStem>(1, *it)));
            }
        }

        // Find the end of the beam group.
        beamableGroupEnd = std::find_if(beamableGroupStart, stems.end(),
                                        std::not1(std::ptr_fun(&NoteStem::isBeamable)));

        if (beamableGroupStart != beamableGroupEnd)
        {
            std::vector<NoteStem> stems(beamableGroupStart, beamableGroupEnd);
            groups.push_back(BeamGroup(stems));
        }
    }
}

QChar StdNotationNote::getNoteHeadSymbol() const
{
    return myNoteHeadSymbol;
}

int StdNotationNote::getPosition() const
{
    return myPosition->getPosition();
}

StdNotationNote::AccidentalType StdNotationNote::getAccidentalType() const
{
    return myAccidentalType;
}

QString StdNotationNote::getAccidentalText() const
{
    QString text;

    switch (myAccidentalType)
    {
    case StdNotationNote::NoAccidental:
        // Do nothing.
        break;
    case StdNotationNote::Natural:
        text = QChar(MusicFont::Natural);
        break;
    case StdNotationNote::Sharp:
        text = QChar(MusicFont::AccidentalSharp);
        break;
    case StdNotationNote::DoubleSharp:
        text = QChar(MusicFont::AccidentalDoubleSharp);
        break;
    case StdNotationNote::Flat:
        text = QChar(MusicFont::AccidentalFlat);
        break;
    case StdNotationNote::DoubleFlat:
        text = QChar(MusicFont::AccidentalDoubleFlat);
        break;
    }

    return text;
}

bool StdNotationNote::isDotted() const
{
    return myPosition->hasProperty(Position::Dotted);
}

void StdNotationNote::clearAccidental()
{
    myAccidentalType = NoAccidental;
}

void StdNotationNote::showAccidental()
{
    computeAccidentalType(true);
}

bool StdNotationNote::isDoubleDotted() const
{
    return myPosition->hasProperty(Position::DoubleDotted);
}