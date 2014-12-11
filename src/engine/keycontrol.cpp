#include <QtDebug>
#include <QPair>

#include "engine/keycontrol.h"

#include "controlobject.h"
#include "controlpushbutton.h"
#include "controlpotmeter.h"
#include "engine/enginebuffer.h"
#include "track/keyutils.h"

static const int kPakmOffsetScaleReseting = 0;
static const int kPakmAbsoluteScaleNoReset = 1;

KeyControl::KeyControl(QString group,
                       ConfigObject<ConfigValue>* pConfig)
        : EngineControl(group, pConfig),
          m_bOldKeylock(false),
          m_speedSliderPitchRatio(1.0),
          m_iPitchAndKeylockMode(kPakmOffsetScaleReseting) {

    struct PitchTempoRatio pitchRateInfo;
    pitchRateInfo.pitchRatio = 1.0;
    pitchRateInfo.tempoRatio = 1.0;
    pitchRateInfo.keylock = false;
    m_pitchRateInfo.setValue(pitchRateInfo);

    m_pPitch = new ControlPotmeter(ConfigKey(group, "pitch"), -0.25f, 0.25f); // +-3 semitones [4.7 ct]
    // Course adjust by full step.
    m_pPitch->setStepCount(6);
    // Fine adjust with semitone / 4.
    m_pPitch->setSmallStepCount(48);

    connect(m_pPitch, SIGNAL(valueChanged(double)),
            this, SLOT(slotPitchChanged(double)),
            Qt::DirectConnection);

    m_pButtonSyncKey = new ControlPushButton(ConfigKey(group, "sync_key"));
    connect(m_pButtonSyncKey, SIGNAL(valueChanged(double)),
            this, SLOT(slotSyncKey(double)),
            Qt::DirectConnection);

    m_pFileKey = new ControlObject(ConfigKey(group, "file_key"));
    connect(m_pFileKey, SIGNAL(valueChanged(double)),
            this, SLOT(slotFileKeyChanged(double)),
            Qt::DirectConnection);

    m_pEngineKey = new ControlObject(ConfigKey(group, "key"));
    connect(m_pEngineKey, SIGNAL(valueChanged(double)),
            this, SLOT(slotSetEngineKey(double)),
            Qt::DirectConnection);

    m_pEngineKeyDistance = new ControlPotmeter(ConfigKey(group, "visual_key_distance"),
                                               -0.5, 0.5);


    m_pitchAndKeylockMode = new ControlPushButton(ConfigKey(group, "pitchAndKeylockMode"));
    m_pitchAndKeylockMode->setButtonMode(ControlPushButton::TOGGLE);
    connect(m_pitchAndKeylockMode, SIGNAL(valueChanged(double)),
            this, SLOT(slotPitchAndKeylockModeChanged(double)),
            Qt::DirectConnection);


    m_pRateSlider = ControlObject::getControl(ConfigKey(group, "rate"));
    connect(m_pRateSlider, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pRateSlider, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);

    m_pRateRange = ControlObject::getControl(ConfigKey(group, "rateRange"));
    connect(m_pRateRange, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pRateRange, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);

    m_pRateDir = ControlObject::getControl(ConfigKey(group, "rate_dir"));
    connect(m_pRateDir, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pRateDir, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);

    m_pKeylock = ControlObject::getControl(ConfigKey(group, "keylock"));
    connect(m_pKeylock, SIGNAL(valueChanged(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
    connect(m_pKeylock, SIGNAL(valueChangedFromEngine(double)),
            this, SLOT(slotRateChanged()),
            Qt::DirectConnection);
}

KeyControl::~KeyControl() {
    delete m_pPitch;
    delete m_pButtonSyncKey;
    delete m_pFileKey;
    delete m_pEngineKey;
    delete m_pEngineKeyDistance;
}

KeyControl::PitchTempoRatio KeyControl::getPitchTempoRatio() const {
    return m_pitchRateInfo.getValue();
}

void KeyControl::resetPitchToLinear() {
    // TODO(DSC)
}

double KeyControl::getKey() {
    return m_pEngineKey->get();
}

void KeyControl::slotRateChanged() {
    //qDebug() << "KeyControl::slotRateChanged 1" << m_pitchRatio << m_speedSliderPitchRatio;

    struct PitchTempoRatio pitchRateInfo = m_pitchRateInfo.getValue();

    // If rate is not 1.0 then we have to try and calculate the octave change
    // caused by it.
    pitchRateInfo.tempoRatio = 1.0 + m_pRateDir->get() * m_pRateRange->get() * m_pRateSlider->get();
    pitchRateInfo.keylock = m_pKeylock->toBool();

    // |-----------------------|-----------------|
    //   SpeedSliderPitchRatio   pitchTweakRatio
    //
    // |-----------------------------------------|
    //   m_pitchRatio
    //
    //                         |-----------------|
    //                           m_pPitch (0)
    //
    // |-----------------------------------------|
    //   m_pPitch (1)

    // here is a possible race condition, if the pitch is changed in between.
    // but it cannot happen if rate and pitch is set from the same thread

    double pitchTweakRatio = pitchRateInfo.pitchRatio / m_speedSliderPitchRatio;

    if (pitchRateInfo.keylock) {
        if (!m_bOldKeylock) {
            // Enabling Keylock
            if (m_iPitchAndKeylockMode == kPakmAbsoluteScaleNoReset) {
                // Lock at current pitch
                m_speedSliderPitchRatio = pitchRateInfo.tempoRatio;
            } else {
                // kPakmOffsetScaleReseting
                // Lock at original track pitch
                m_speedSliderPitchRatio = 1.0;
            }
        }
    } else {
        // !bKeylock
        if (m_bOldKeylock) {
            // Disabling Keylock
            if (m_iPitchAndKeylockMode == kPakmAbsoluteScaleNoReset) {
                // reset to linear pitch
                pitchTweakRatio = 1.0;
                // For not resetting to linear pitch:
                // Adopt speedPitchRatio change as pitchTweakRatio
                //pitchTweakRatio *= (m_speedSliderPitchRatio / pitchRateInfo.tempoRatio);
            }
        }
        m_speedSliderPitchRatio = pitchRateInfo.tempoRatio;
    }
    m_bOldKeylock = pitchRateInfo.keylock;

    pitchRateInfo.pitchRatio = pitchTweakRatio * m_speedSliderPitchRatio;

    double pitchOctaves = KeyUtils::powerOf2ToOctaveChange(pitchRateInfo.pitchRatio);
    double dFileKey = m_pFileKey->get();
    updateKeyCOs(dFileKey, pitchOctaves);

    if (m_iPitchAndKeylockMode == kPakmAbsoluteScaleNoReset) {
        // Pitch scale is always 0 at original pitch
        m_pPitch->set(pitchOctaves);
    }

    // store so that the results are availabe to the engine at once
    m_pitchRateInfo.setValue(pitchRateInfo);

    // qDebug() << "KeyControl::slotRateChanged 2" << m_pitchRatio << m_speedSliderPitchRatio;
}

void KeyControl::slotPitchAndKeylockModeChanged(double value) {
    //qDebug() << "KeyControl::slotPitchAndKeylockModeChanged 1" << m_pitchRatio << m_speedSliderPitchRatio;

    struct PitchTempoRatio pitchRateInfo = m_pitchRateInfo.getValue();

    if (value == 0.0 && m_iPitchAndKeylockMode == kPakmAbsoluteScaleNoReset) {
        // absolute mode to offset mode
        if (pitchRateInfo.keylock) {
            m_speedSliderPitchRatio = 1.0;
        }
    }
    m_iPitchAndKeylockMode = (int)value;
    slotRateChanged();

    if (m_iPitchAndKeylockMode == kPakmOffsetScaleReseting) {
        // Normally Pitch slider is not moved in this mode,
        // so we need to correct it manually here
        pitchRateInfo = m_pitchRateInfo.getValue();
        double pitchTweakRatio = pitchRateInfo.pitchRatio / m_speedSliderPitchRatio;
        double pitchTweakOctaves = KeyUtils::powerOf2ToOctaveChange(pitchTweakRatio);
        m_pPitch->set(pitchTweakOctaves);
    }

    //qDebug() << "KeyControl::slotPitchAndKeylockModeChanged 2" << m_pitchRatio << m_speedSliderPitchRatio;
}

void KeyControl::slotFileKeyChanged(double value) {
    struct PitchTempoRatio pitchRateInfo = m_pitchRateInfo.getValue();
    updateKeyCOs(value, KeyUtils::powerOf2ToOctaveChange(pitchRateInfo.pitchRatio));
}

void KeyControl::updateKeyCOs(double fileKeyNumeric, double pitch) {
    mixxx::track::io::key::ChromaticKey fileKey =
            KeyUtils::keyFromNumericValue(fileKeyNumeric);

    QPair<mixxx::track::io::key::ChromaticKey, double> adjusted =
            KeyUtils::scaleKeyOctaves(fileKey, pitch);
    m_pEngineKey->set(KeyUtils::keyToNumericValue(adjusted.first));
    double diff_to_nearest_full_key = adjusted.second;
    m_pEngineKeyDistance->set(diff_to_nearest_full_key);
}


void KeyControl::slotSetEngineKey(double key) {
    Q_UNUSED(key);
    // TODO(rryan): set m_pPitch to match the desired key.
}

void KeyControl::slotPitchChanged(double pitch) {
    //qDebug() << "KeyControl::slotPitchChanged 1" << m_pitchRatio << m_speedSliderPitchRatio;

    double pitchTweakRatio = KeyUtils::octaveChangeToPowerOf2(pitch);
    if (m_iPitchAndKeylockMode == kPakmOffsetScaleReseting) {
        // Pitch slider presents only the offset, calc absolute pitch
        pitchTweakRatio *= m_speedSliderPitchRatio;
    }

    struct PitchTempoRatio pitchRateInfo = m_pitchRateInfo.getValue();
    pitchRateInfo.pitchRatio = pitchTweakRatio;
    m_pitchRateInfo.setValue(pitchRateInfo);

    double dFileKey = m_pFileKey->get();
    updateKeyCOs(dFileKey, KeyUtils::powerOf2ToOctaveChange(pitchTweakRatio));

    //qDebug() << "KeyControl::slotPitchChanged 2" << m_pitchRatio << m_speedSliderPitchRatio;
}

void KeyControl::slotSyncKey(double v) {
    if (v > 0) {
        EngineBuffer* pOtherEngineBuffer = pickSyncTarget();
        syncKey(pOtherEngineBuffer);
    }
}

bool KeyControl::syncKey(EngineBuffer* pOtherEngineBuffer) {
    if (!pOtherEngineBuffer) {
        return false;
    }

    mixxx::track::io::key::ChromaticKey thisFileKey =
            KeyUtils::keyFromNumericValue(m_pFileKey->get());

    // Get the sync target's effective key, since that is what we aim to match.
    double dKey = ControlObject::get(ConfigKey(pOtherEngineBuffer->getGroup(), "key"));
    mixxx::track::io::key::ChromaticKey otherKey =
            KeyUtils::keyFromNumericValue(dKey);
    double otherDistance = ControlObject::get(ConfigKey(pOtherEngineBuffer->getGroup(), "visual_key_distance"));


    if (thisFileKey == mixxx::track::io::key::INVALID ||
        otherKey == mixxx::track::io::key::INVALID) {
        return false;
    }

    int stepsToTake = KeyUtils::shortestStepsToCompatibleKey(thisFileKey, otherKey);
    double pitchToTakeOctaves = (stepsToTake + otherDistance) / 12.0;

    if (m_iPitchAndKeylockMode == kPakmOffsetScaleReseting) {
        double pitchToTakeRatio = KeyUtils::octaveChangeToPowerOf2(pitchToTakeOctaves);
        double pitchTweakRatio = pitchToTakeRatio / m_speedSliderPitchRatio;
        pitchToTakeOctaves = KeyUtils::powerOf2ToOctaveChange(pitchTweakRatio);
    }

    m_pPitch->set(pitchToTakeOctaves);
    slotPitchChanged(pitchToTakeOctaves);
    return true;
}

void KeyControl::collectFeatures(GroupFeatureState* pGroupFeatures) const {
    mixxx::track::io::key::ChromaticKey fileKey =
            KeyUtils::keyFromNumericValue(m_pFileKey->get());
    if (fileKey != mixxx::track::io::key::INVALID) {
        pGroupFeatures->has_file_key = true;
        pGroupFeatures->file_key = fileKey;
    }

    mixxx::track::io::key::ChromaticKey key =
            KeyUtils::keyFromNumericValue(m_pEngineKey->get());
    if (key != mixxx::track::io::key::INVALID) {
        pGroupFeatures->has_key = true;
        pGroupFeatures->key = key;
    }
}
