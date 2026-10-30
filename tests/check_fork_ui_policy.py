#!/usr/bin/env python3
"""Source/UI policy guard: donation UX removed, original attribution retained."""
from pathlib import Path
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
for file, forbidden in (("mainwindow.ui", "actionDonate"), ("aboutdialog.ui", "donateButton")):
    tree = ET.parse(root / "src/tiled" / file)
    assert not any(node.attrib.get("name") == forbidden for node in tree.iter()), file
for file in ("mainwindow.cpp", "mainwindow.h", "aboutdialog.cpp", "aboutdialog.h",
             "preferences.cpp", "preferences.h", "libtilededitor.qbs"):
    text = (root / "src/tiled" / file).read_text()
    for obsolete in ("showDonationPopup", "shouldShowDonationReminder", "actionDonate",
                     "donateButton", "DonationDialogTime", "PatreonDialogTime", "donationpopup"):
        assert obsolete not in text, (file, obsolete)
about = (root / "src/tiled/aboutdialog.cpp").read_text()
for retained in ("Copyright 2008-", "Thorbj&oslash;rn Lindeijer", "AUTHORS", "GPL", "COPYING"):
    assert retained in about, retained
assert (root / "AUTHORS").is_file()
assert (root / "COPYING").is_file()
print("Donation UX/reminder source policy passed; About attribution and GPL references retained.")
