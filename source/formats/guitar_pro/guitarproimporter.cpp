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
  
#include "guitarproimporter.h"

#include <formats/guitar_pro/document.h>
#include <formats/guitar_pro/inputstream.h>
#include <score/score.h>
#include <score/utils.h>

static const int POSITIONS_PER_SYSTEM = 35;

GuitarProImporter::GuitarProImporter()
    : FileFormatImporter(
          FileFormat("Guitar Pro 3, 4, 5", { "gp3", "gp4", "gp5" }))
{
}

void GuitarProImporter::load(const std::string &filename, Score &score)
{
    std::ifstream in(filename, std::ios::binary | std::ios::in);
    Gp::InputStream stream(in);

    Gp::Document document;
    document.load(stream);

    ScoreInfo info;
    convertHeader(document.myHeader, info);
    score.setScoreInfo(info);

    convertPlayers(document, score);
    convertScore(document, score);

    // Automatically set the rehearsal sign letters to "A", "B", etc.
    ScoreUtils::adjustRehearsalSigns(score);
}

void GuitarProImporter::convertHeader(const Gp::Header &header, ScoreInfo &info)
{
    SongData song;

    song.setTitle(header.myTitle);
    song.setArtist(header.myArtist);
    song.setAudioReleaseInfo(SongData::AudioReleaseInfo(
        SongData::AudioReleaseInfo::ReleaseType::Single, header.myAlbum,
        boost::gregorian::day_clock::local_day().year(), false));
    song.setAuthorInfo(
        SongData::AuthorInfo(header.myComposer, header.myLyricist));
    song.setCopyright(header.myCopyright);
    song.setTranscriber(header.myTranscriber);

    // Merge the instructions and comments into the performance notes.
    std::string comments;
    if (!header.myInstructions.empty())
        comments += header.myInstructions + "\n";
    for (const std::string &comment : header.myNotices)
        comments += comment;
    song.setPerformanceNotes(comments);

    // Merge lyrics together.
    std::string lyrics;
    for (const Gp::Header::LyricLine &line : header.myLyrics)
        lyrics += line.myContents;
    song.setLyrics(lyrics);

    info.setSongData(song);
}

void GuitarProImporter::convertPlayers(const Gp::Document &doc, Score &score)
{
    for (const Gp::Track &track : doc.myTracks)
    {
        Player player;
        Instrument instrument;
        Tuning tuning;

        player.setDescription(track.myName);
        instrument.setDescription(track.myName);

        tuning.setNotes(track.myTuning);
        tuning.setCapo(track.myCapo);

        const Gp::Channel &channel = doc.myChannels[track.myChannelIndex];
        instrument.setMidiPreset(channel.myInstrument);
        player.setMaxVolume(channel.myVolume);
        player.setPan(channel.myBalance);

        player.setTuning(tuning);
        score.insertPlayer(player);
        score.insertInstrument(instrument);
    }
}

int GuitarProImporter::convertBarline(const Gp::Measure &measure,
                                      const Gp::Measure *prevMeasure,
                                      System &system, int start, int end,
                                      KeySignature &lastKeySig,
                                      TimeSignature &lastTimeSig)
{
    Barline bar;
    bar.setPosition(start);

    if (prevMeasure && prevMeasure->myIsDoubleBar)
        bar.setBarType(Barline::DoubleBar);
    else if (measure.myIsRepeatBegin)
        bar.setBarType(Barline::RepeatStart);

    if (measure.myMarker)
        bar.setRehearsalSign(RehearsalSign("", *measure.myMarker));

    if (measure.myKeyChange)
    {
        KeySignature key;
        key.setVisible(true);

        // Guitar Pro uses 0 for C, 1 for G, ..., -1 for F, -2 for Bb, ...,
        // whereas Power Tab uses 0 for 1, 1 for G, 1 for F, etc.
        const int numAccidentals = measure.myKeyChange.get().first;

        // Create a cancellation if necessary.
        if (numAccidentals == 0 && lastKeySig.getNumAccidentals() > 0)
        {
            key.setCancellation();
            key.setNumAccidentals(lastKeySig.getNumAccidentals());
            key.setSharps(lastKeySig.usesSharps());
        }
        else
        {
            key.setSharps(numAccidentals >= 0);
            key.setNumAccidentals(std::abs(measure.myKeyChange->first));
        }

        key.setKeyType(
            static_cast<KeySignature::KeyType>(measure.myKeyChange->second));
        bar.setKeySignature(key);

        // Future copies of this key signature should not be shown.
        key.setVisible(false);
        lastKeySig = key;
    }
    else
        bar.setKeySignature(lastKeySig);

    if (measure.myTimeSignatureChange)
    {
        TimeSignature time;
        time.setVisible(true);

        time.setBeatsPerMeasure(measure.myTimeSignatureChange->first);
        time.setNumPulses(measure.myTimeSignatureChange->first);
        time.setBeatValue(measure.myTimeSignatureChange->second);
        bar.setTimeSignature(time);

        // Future copies of this time signature should not be shown.
        time.setVisible(false);
        lastTimeSig = time;
    }
    else
        bar.setTimeSignature(lastTimeSig);

    // Insert at the correct location.
    if (start == 0)
        system.getBarlines().front() = bar;
    else
        system.insertBarline(bar);

    if (measure.myRepeatEnd)
    {
        bar.setPosition(end);
        bar.setBarType(Barline::RepeatEnd);
        bar.setRepeatCount(measure.myRepeatEnd.get());

        // Hide key signatures and time signatures.
        KeySignature key(bar.getKeySignature());
        key.setVisible(false);
        bar.setKeySignature(key);
        TimeSignature time(bar.getTimeSignature());
        time.setVisible(false);
        bar.setTimeSignature(time);

        // Insert at the correct location.
        if (end > POSITIONS_PER_SYSTEM)
            system.getBarlines().back() = bar;
        else
            system.insertBarline(bar);

        ++end;
    }

    return end;
}

void GuitarProImporter::convertAlternateEndings(const Gp::Measure &measure,
                                                System &system, int position)
{
    // Each bit represent an alternate ending from 1 to 8.
    if (measure.myAlternateEnding)
    {
        AlternateEnding ending(position);;

        std::bitset<8> bits(measure.myAlternateEnding.get());
        for (int i = 0; i < 8; ++i)
        {
            if (bits.test(i))
                ending.addNumber(i + 1);
        }

        system.insertAlternateEnding(ending);
    }
}

void GuitarProImporter::convertScore(const Gp::Document &doc, Score &score)
{
    System system;
    KeySignature lastKeySig;
    TimeSignature lastTimeSig;

    // Add a staff for each player.
    for (const Player &player : score.getPlayers())
        system.insertStaff(Staff(player.getTuning().getStringCount()));

    // Add initial tempo marker.
    {
        TempoMarker marker;
        marker.setPosition(0);
        marker.setBeatsPerMinute(doc.myStartTempo);
        system.insertTempoMarker(marker);
    }

    // Add an initial player change.
    {
        // Set up an initial player change.
        PlayerChange change;
        for (int i = 0; i < score.getPlayers().size(); ++i)
            change.insertActivePlayer(i, ActivePlayer(i, i));
        system.insertPlayerChange(change);
    }

    int startPos = 0;
    for (int m = 0; m < doc.myMeasures.size(); ++m)
    {
        const Gp::Measure &measure = doc.myMeasures[m];

        // Try to create a new system every so often.
        if (startPos > POSITIONS_PER_SYSTEM)
        {
            system.getBarlines().back().setPosition(startPos + 1);
            score.insertSystem(system);
            system = System();

            // Add a staff for each player.
            for (const Player &player : score.getPlayers())
                system.insertStaff(Staff(player.getTuning().getStringCount()));

            startPos = 0;
        }

        // For each player, import the notes from the current measure.
        int nextPos = startPos;
        for (int i = 0; i < score.getPlayers().size(); ++i)
        {
            const Tuning &tuning = score.getPlayers()[i].getTuning();
            Staff &staff = system.getStaves()[i];
            const Gp::Staff &gp_staff = measure.myStaves[i];

            for (int v = 0; v < gp_staff.myVoices.size(); ++v)
            {
                // Start inserting notes after the barline.
                int currentPos = (startPos != 0) ? startPos + 1 : 0;
                Voice &voice = staff.getVoices()[v];

                for (const Gp::Beat &beat : gp_staff.myVoices[v])
                    currentPos = convertBeat(beat, system, voice, currentPos);

                // TODO - convert irregular groups.

                nextPos = std::max(nextPos, currentPos);
            }
        }

        // Import the barline, key signature, etc.
        const Gp::Measure *prevMeasure =
            (m > 0) ? &doc.myMeasures[m - 1] : nullptr;
        nextPos = convertBarline(measure, prevMeasure, system, startPos,
                                 nextPos, lastKeySig, lastTimeSig);

        // Check for alternate endings.
        convertAlternateEndings(measure, system, startPos);

        // TODO - import tempo markers.

        startPos = nextPos;
    }

    // Insert the final system.
    system.getBarlines().back().setPosition(startPos + 1);
    score.insertSystem(system);
}

int GuitarProImporter::convertBeat(const Gp::Beat &beat, System &system,
                                   Voice &voice, int position)
{
    if (beat.myText &&
        !ScoreUtils::findByPosition(system.getTextItems(), position))
    {
        TextItem text(position, *beat.myText);
        system.insertTextItem(text);
    }

    if (beat.myIsEmpty)
        return position + 1;

    // TODO - handle grace notes.

    Position pos(position);
    pos.setRest(beat.myIsRest);
    pos.setDurationType(static_cast<Position::DurationType>(beat.myDuration));
    pos.setProperty(Position::Dotted, beat.myIsDotted);
    pos.setProperty(Position::PickStrokeUp, beat.myPickstrokeUp);
    pos.setProperty(Position::PickStrokeDown, beat.myPickstrokeDown);
    pos.setProperty(Position::Tap, beat.myIsTapped);

    bool hasVibratoNote = beat.myIsVibrato;
    bool hasTremoloPickedNote = beat.myIsTremoloPicked;
    bool hasStaccatoNote = false;
    bool hasMarcatoNote = false;
    bool hasSforzandoNote = false;
    bool hasPalmMutedNote = false;
    bool hasLetRingNote = false;

    for (const Gp::Note &gp_note : beat.myNotes)
    {
        Note note;
        note.setString(gp_note.myString);
        note.setFretNumber(gp_note.myFret);

        note.setProperty(Note::Tied, gp_note.myIsTied);
        note.setProperty(Note::Muted, gp_note.myIsMuted);
        note.setProperty(Note::HammerOnOrPullOff,
                         gp_note.myIsHammerOnOrPullOff);
        note.setProperty(Note::NaturalHarmonic, gp_note.myIsNaturalHarmonic);
        note.setProperty(Note::GhostNote, gp_note.myIsGhostNote);
        note.setProperty(Note::GhostNote, gp_note.myIsGhostNote);

        if (gp_note.myTrilledFret)
            note.setTrilledFret(*gp_note.myTrilledFret);

        // TODO - copy harmonics from the beat.
        // TODO - import bends.
        // TODO - import slides.
        // TODO - import dynamics.
        // TODO - figure out how to import octave (8va) symbols.

        hasVibratoNote |= gp_note.myIsVibrato;
        hasTremoloPickedNote |= gp_note.myIsTremoloPicked;
        hasStaccatoNote |= gp_note.myIsStaccato;
        hasMarcatoNote |= gp_note.myHasAccent;
        hasSforzandoNote |= gp_note.myHasHeavyAccent;
        hasPalmMutedNote |= gp_note.myHasPalmMute;
        hasLetRingNote |= gp_note.myIsLetRing;

        pos.insertNote(note);
    }

    pos.setProperty(Position::Vibrato, hasVibratoNote);
    pos.setProperty(Position::TremoloPicking, hasTremoloPickedNote);
    pos.setProperty(Position::Staccato, hasStaccatoNote);
    pos.setProperty(Position::Marcato, hasMarcatoNote);
    pos.setProperty(Position::Sforzando, hasSforzandoNote);
    pos.setProperty(Position::PalmMuting, hasPalmMutedNote);
    pos.setProperty(Position::LetRing, hasLetRingNote);

    voice.insertPosition(pos);
    ++position;
    return position;
}

#if 0
void GuitarProImporter::readOldStyleChord(Gp::InputStream &stream,
                                          const Tuning & /*tuning*/)
{
// TODO - chord diagrams are not yet supported for the new file format.
#if 0
    std::vector<uint8_t> fretNumbers(tuning.getStringCount(),
                                     ChordDiagram::stringMuted);

    ChordDiagram diagram(0, fretNumbers);
#endif
    stream.readString(); // chord diagram name

    const uint32_t baseFret = stream.read<uint32_t>();
#if 0
    diagram.SetTopFret(baseFret);
#endif

    if (baseFret != 0)
    {
        for (int i = 0; i < Gp::NumberOfStringsGp3; ++i)
        {
#if 0
            diagram.SetFretNumber(i, stream.read<uint32_t>());
#else
            stream.read<uint32_t>();
#endif
        }
    }
}

void GuitarProImporter::readTremoloBar(Gp::InputStream &stream,
                                       Position & /*position*/)
{
// TODO - implement tremolo bar support.
#if 1
    if (stream.version != Gp::Version3)
        stream.read<uint8_t>();

    stream.read<int32_t>();

    if (stream.version >= Gp::Version4)
    {
        const uint32_t numPoints = stream.read<uint32_t>();
        for (uint32_t i = 0; i < numPoints; i++)
        {
            stream.skip(4); // time relative to the previous point
            stream.skip(4); // bend value
            stream.skip(1); // vibrato (used for bend, not for tremolo bar)
        }
    }
#else
    uint8_t eventType;
    if (stream.version == Gp::Version3)
    {
        eventType = Position::dip;
    }
    else
    {
        eventType = convertTremoloEventType(stream.read<uint8_t>());
    }

    const int32_t pitch = convertBendPitch(stream.read<int32_t>());

    position.SetTremoloBar(eventType, 0, pitch);

    if (stream.version >= Gp::Version4)
    {
        const uint32_t numPoints =
            stream.read<uint32_t>(); // number of bend points
        for (uint32_t i = 0; i < numPoints; i++)
        {
            stream.skip(4); // time relative to the previous point
            stream.skip(4); // bend value
            stream.skip(1); // vibrato (used for bend, not for tremolo bar)
        }
    }
#endif
}

void GuitarProImporter::readMixTableChangeEvent(
    Gp::InputStream &stream, boost::optional<TempoMarker> &tempoMarker)
{
    // TODO - implement conversions for this.

    stream.read<int8_t>(); // instrument

    if (stream.version > Gp::Version4)
        stream.skip(16); // RSE Info???

    int8_t volume = stream.read<int8_t>(); // volume
    int8_t pan = stream.read<uint8_t>(); // pan
    int8_t chorus = stream.read<uint8_t>(); // chorus
    int8_t reverb = stream.read<uint8_t>(); // reverb
    int8_t phaser = stream.read<uint8_t>(); // phaser
    int8_t tremolo = stream.read<uint8_t>(); // tremolo

    if (stream.version > Gp::Version4)
        std::cerr << stream.readString() << std::endl; // tempo name???

    // New tempo.
    int32_t tempo = stream.read<int32_t>();
    if (tempo > 0)
    {
        tempoMarker = TempoMarker();
        tempoMarker->setBeatsPerMinute(tempo);
    }

    if (volume >= 0)
        stream.read<uint8_t>(); // volume change duration

    if (pan >= 0)
        stream.read<uint8_t>(); // pan change duration

    if (chorus >= 0)
        stream.read<uint8_t>(); // chorus change duration

    if (reverb >= 0)
        stream.read<uint8_t>(); // reverb change duration

    if (phaser >= 0)
        stream.read<uint8_t>(); // phaser change duration

    if (tremolo >= 0)
        stream.read<uint8_t>(); // tremolo change duration

    if (tempo >= 0)
    {
        stream.skip(1); // tempo change duration

        if (stream.version == Gp::Version5_1)
            stream.skip(1);
    }

    if (stream.version >= Gp::Version4)
    {
        // Details of score-wide or track-specific changes.
        stream.read<uint8_t>();
    }

    if (stream.version > Gp::Version4)
    {
        stream.skip(1);
        if (stream.version == Gp::Version5_1)
        {
            std::cerr << stream.readString() << std::endl;
            std::cerr << stream.readString() << std::endl;
        }
    }
}

// TODO - implement tremolo bar support.
#if 0
uint8_t GuitarProImporter::convertTremoloEventType(uint8_t gpEventType)
{
    switch (gpEventType)
    {
        case Gp::Dip:
            return Position::dip;

        case Gp::Dive:
            return Position::diveAndRelease;

        case Gp::ReleaseUp:
        case Gp::ReleaseDown:
            return Position::release;

        case Gp::InvertedDip:
            return Position::invertedDip;

        case Gp::TremoloReturn:
            return Position::returnAndRelease;

        default:
        {
            std::cerr << "Invalid tremolo bar event type: " << (int)gpEventType
                      << std::endl;
            return Position::diveAndRelease;
        }
    }
}
#endif

void GuitarProImporter::readNotes(Gp::InputStream &stream, Position &position)
{
    const Gp::Flags stringsPlayed = stream.read<uint8_t>();

    for (int i = Gp::NumberOfStrings - 1; i >= 0; i--)
    {
        if (stringsPlayed.test(i))
        {
            Note note;

            note.setString(Gp::NumberOfStrings - i - 1);

            const Gp::Flags flags = stream.read<uint8_t>();

            position.setProperty(Position::Marcato,
                                 flags.test(Gp::AccentedNote));

            if (stream.version > Gp::Version4)
            {
                position.setProperty(Position::Sforzando,
                                     flags.test(Gp::HeavyAccentedNote));
            }

            note.setProperty(Note::GhostNote, flags.test(Gp::GhostNote));

            // Ignore dotted note flag - already handled elsewhere for the
            // Position object.

            if (flags.test(Gp::NoteType))
            {
                const uint8_t noteType = stream.read<uint8_t>();
                note.setProperty(Note::Tied, noteType == Gp::TiedNote);
                note.setProperty(Note::Muted, noteType == Gp::MutedNote);
            }

            if (stream.version <= Gp::Version4 &&
                flags.test(Gp::TimeIndependentDuration))
            {
                // This is a repeat of the Position duration -- ignore.
                stream.skip(1);
                stream.skip(1);
            }

            if (flags.test(Gp::Dynamic))
            {
                stream.skip(1); // TODO - record the dynamic.
            }

            // If there is a non-empty note, read the fret number.
            if (flags.test(Gp::NoteType))
            {
                const uint8_t fret = stream.read<uint8_t>();
                note.setFretNumber(fret);
            }

            if (flags.test(Gp::FingeringType))
            {
                // left and right hand fingerings -- ignore
                stream.skip(1);
                stream.skip(1);
            }

            if (stream.version > Gp::Version4)
            {
                // TODO - figure out what this data is used for in GP5
                if (flags.test(Gp::TimeIndependentDuration))
                {
                    stream.skip(8);
                }
                stream.skip(1);
            }

            if (flags.test(Gp::NoteEffects))
            {
                if (stream.version >= Gp::Version4)
                    readNoteEffects(stream, position, note);
                else if (stream.version == Gp::Version3)
                    readNoteEffectsGp3(stream, position, note);
            }

            position.insertNote(note);
        }
    }
}

void GuitarProImporter::readNoteEffects(Gp::InputStream &stream,
                                        Position &position, Note &note)
{
    const Gp::Flags header1 = stream.read<uint8_t>();
    const Gp::Flags header2 = stream.read<uint8_t>();

    if (header1.test(Gp::HasBend))
        readBend(stream, note);

    if (header1.test(Gp::HasGraceNote))
    {
        // TODO - handle grace notes.
        stream.skip(1); // fret number grace note is made from
        stream.skip(1); // grace note dynamic
        stream.skip(1); // transition type
        stream.skip(1); // duration
        if (stream.version > Gp::Version4)
            stream.skip(1); // flags for GP5
    }

    if (header2.test(Gp::HasTremoloPicking))
    {
        // Ignore - Power Tab does not allow different values for the tremolo
        // picking duration (e.g. eighth notes).
        stream.skip(1);
        position.setProperty(Position::TremoloPicking);
    }

    if (header2.test(Gp::HasSlide))
        readSlide(stream, note);

    if (header2.test(Gp::HasHarmonic))
        readHarmonic(stream, note);

    if (header2.test(Gp::HasTrill))
    {
        const uint8_t fret = stream.read<uint8_t>();
        note.setTrilledFret(fret);

        // Trill duration - not supported in Power Tab.
        stream.skip(1);
    }

    position.setProperty(Position::LetRing, header1.test(Gp::HasLetRing));

    note.setProperty(Note::HammerOnOrPullOff,
                     header1.test(Gp::HasHammerOnOrPullOff));

    position.setProperty(Position::Vibrato, header2.test(Gp::HasVibrato));
    position.setProperty(Position::PalmMuting, header2.test(Gp::HasPalmMute));
    position.setProperty(Position::Staccato, header2.test(Gp::HasStaccato));
}

void GuitarProImporter::readNoteEffectsGp3(Gp::InputStream &stream,
                                           Position &position, Note &note)
{
    const Gp::Flags flags = stream.read<uint8_t>();

    position.setProperty(Position::LetRing, flags.test(Gp::HasLetRing));
    note.setProperty(Note::HammerOnOrPullOff,
                     flags.test(Gp::HasHammerOnOrPullOff));

    if (flags.test(Gp::HasSlideOutVer3))
        note.setProperty(Note::SlideOutOfDownwards);

    if (flags.test(Gp::HasBend))
        readBend(stream, note);

    if (flags.test(Gp::HasGraceNote))
    {
        stream.read<uint8_t>(); // fret number grace note is made from
        stream.read<uint8_t>(); // grace note dynamic
        stream.read<uint8_t>(); // transition type
        stream.read<uint8_t>(); // duration
        // TODO - will need to add an extra note to be the grace note
    }
}

void GuitarProImporter::readSlide(Gp::InputStream &stream, Note &note)
{
    int8_t slideValue = stream.read<int8_t>();

    if (stream.version <= Gp::Version4)
    {
        /* Slide values are as follows:
            -2 : slide into from above
            -1 : slide into from below
            0  : no slide
            1  : shift slide
            2  : legato slide
            3  : slide out of downwards
            4  : slide out of upwards
        */
        switch (slideValue)
        {
            case -2:
                note.setProperty(Note::SlideIntoFromAbove);
                break;
            case -1:
                note.setProperty(Note::SlideIntoFromBelow);
                break;
            case 1:
                note.setProperty(Note::ShiftSlide);
                break;
            case 2:
                note.setProperty(Note::LegatoSlide);
                break;
            case 3:
                note.setProperty(Note::SlideOutOfDownwards);
                break;
            case 4:
                note.setProperty(Note::SlideOutOfUpwards);
                break;
        }
    }
    else
    {
        switch (slideValue)
        {
            case 1:
                note.setProperty(Note::ShiftSlide);
                break;
            case 2:
                note.setProperty(Note::LegatoSlide);
                break;
            case 4:
                note.setProperty(Note::SlideOutOfDownwards);
                break;
            case 8:
                note.setProperty(Note::SlideOutOfUpwards);
                break;
            case 16:
                note.setProperty(Note::SlideIntoFromBelow);
                break;
            case 32:
                note.setProperty(Note::SlideIntoFromAbove);
                break;
        }
    }
}

void GuitarProImporter::readHarmonic(Gp::InputStream &stream, Note &note)
{
    const uint8_t harmonic = stream.read<uint8_t>();

    if (harmonic == Gp::NaturalHarmonic)
    {
        note.setProperty(Note::NaturalHarmonic);
    }
    else if (harmonic == Gp::TappedHarmonic)
    {
        if (stream.version > Gp::Version4)
        {
            std::cerr << "Tapped Harmonic Data: " << (int)stream.read<uint8_t>()
                      << std::endl;
        }

        // TODO - fix this.
        note.setTappedHarmonicFret(note.getFretNumber());
    }
    // TODO - handle artificial harmonics for GP3, GP4, and GP5
    else if (harmonic == Gp::ArtificalHarmonicGp5)
    {
        stream.skip(3);
    }
}

void GuitarProImporter::readBend(Gp::InputStream &stream, Note &)
{
    // TODO - perform conversion for bends

    stream.read<uint8_t>(); // bend type
    stream.read<uint32_t>(); // bend height

    const uint32_t numPoints = stream.read<uint32_t>(); // number of bend points

    for (uint32_t i = 0; i < numPoints; i++)
    {
        stream.skip(4); // time relative to the previous point
        stream.skip(4); // bend position
        stream.skip(1); // bend vibrato
    }
}

#if 0
/// Converts bend pitches from GP format (25 per quarter tone) to PTB (1 per
/// quarter tone)
uint8_t GuitarProImporter::convertBendPitch(int32_t gpBendPitch)
{
    return std::abs(gpBendPitch / 25);
}
#endif
#endif
