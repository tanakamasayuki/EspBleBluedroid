#include <EspBleBluedroid.h>

EspBleBluedroid bluetooth;

struct ProfileEntry
{
  const char *name;
  EspBluedroidClassicProfile profile;
};

const ProfileEntry profiles[] = {
  {"SPP", EspBluedroidClassicProfile::Spp},
  {"A2DP Sink", EspBluedroidClassicProfile::A2dpSink},
  {"A2DP Source", EspBluedroidClassicProfile::A2dpSource},
  {"AVRCP Controller", EspBluedroidClassicProfile::AvrcpController},
  {"AVRCP Target", EspBluedroidClassicProfile::AvrcpTarget},
  {"HID Device / GamePad", EspBluedroidClassicProfile::HidDevice},
  {"HID Host", EspBluedroidClassicProfile::HidHost},
  {"HFP Hands-Free", EspBluedroidClassicProfile::HfpHandsFree},
  {"HFP Audio Gateway", EspBluedroidClassicProfile::HfpAudioGateway},
  {"PBAP Client", EspBluedroidClassicProfile::PbapClient},
  {"MIDI", EspBluedroidClassicProfile::Midi},
};

const char *statusName(EspBluedroidClassicProfileStatus status)
{
  switch (status)
  {
    case EspBluedroidClassicProfileStatus::Supported:
      return "supported";
    case EspBluedroidClassicProfileStatus::LibraryNotImplemented:
      return "library-not-implemented";
    case EspBluedroidClassicProfileStatus::CoreDisabled:
      return "core-disabled";
    case EspBluedroidClassicProfileStatus::CoreApiUnavailable:
      return "core-api-unavailable";
    case EspBluedroidClassicProfileStatus::NoStandardProfile:
      return "no-standard-profile";
  }
  return "unknown";
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  for (const ProfileEntry &entry : profiles)
  {
    const EspBluedroidClassicProfileSupport support =
      bluetooth.classic().profileSupport(entry.profile);
    Serial.printf(
      "%s: %s\n  %s\n",
      entry.name,
      statusName(support.status),
      support.reason.c_str());
  }
}

void loop()
{
}

