# Desktop GUI dependencies

The GUI uses dynamically loaded Qt Widgets through PySide6-Essentials and
Shiboken6. It does not use Qt WebEngine, a WebView, or an embedded browser.
The project's own MIT license does not replace dependency licenses.

- Qt / PySide6 / Shiboken6: LGPLv3 or their alternative GPL/commercial terms.
  [Qt for Python licenses](https://doc.qt.io/qtforpython-6/licenses.html),
  [Qt licensing](https://doc.qt.io/qt-6/licensing.html),
  [source](https://code.qt.io/cgit/pyside/pyside-setup.git/).
- PySerial: BSD-3-Clause; https://github.com/pyserial/pyserial.
- Bleak: MIT; https://github.com/hbldh/bleak.
- Keyring: MIT; https://github.com/jaraco/keyring.
- minimp3: CC0; the vendored header retains its original notice.
- CPython: PSF license; https://docs.python.org/3/license.html.
- PyInstaller: GPL with its bootloader exception permitting packaged programs;
  https://pyinstaller.org/en/stable/license.html.

GUI bundles use **onedir**, leaving shared Qt/PySide libraries as separate
replaceable files. Keep the complete directory, license texts, bundled third-party
notices, and build instructions when redistributing. Do not prohibit debugging
for modifications of LGPL components. The build emits a dependency/version
inventory and copies license files present in installed dependency distributions.

Before public binary publication, review the licenses/notices of the actual
bundled Qt build and provide the corresponding source/required offers under the
chosen terms. The workflow and a local UI preview alone are not a distribution
compliance certification. This change does not publish binaries or alter the
project's license.
