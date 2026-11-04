# Qt 6.11.3 Cocoa accessibility fix

This is the production-file portion of Qt's accepted fix
[8148c48deb8e4345bacb797cb75c011df6f797fa](https://github.com/qt/qtbase/commit/8148c48deb8e4345bacb797cb75c011df6f797fa)
by Yuri Barreira, reviewed by Volker Hilsheimer, for QTBUG-149612.
Qt source copyright and licensing remain unchanged (Qt Commercial or LGPL-3.0 /
GPL alternatives as specified in the source). The full source and license files
are included in the official source archive used by the build script.

Native Cocoa accessibility elements must release their native arrays without
deleting QAccessibleInterface objects owned by the item view/cache. This change
only touches the private Cocoa element header/implementation, not QtCore/QtGui
interfaces, instance layouts, or any framework binary. A separate exported
identity function lets this fork reject an accidentally loaded unpatched plugin.

The build clones the complete official 6.11.3 kit, compiles its Cocoa platform
plugin with the matching private headers/frameworks, architectures and deployment
target, then verifies every original Qt framework is unchanged. The complete
Tiled.app is rebuilt, deployed and audited against that kit. Do not manually
copy this plugin into another Qt version or into an existing application.

Pinned source archive SHA256:
`46de0710b594945a5d446ae44354daa386141c6903c53781b2ca547bc62f8f8f`

Production patch SHA256:
`e0f0ddcb7deff7e01094814880ec3835bd4e63012d16af50bc81e39e7cad92ea`
