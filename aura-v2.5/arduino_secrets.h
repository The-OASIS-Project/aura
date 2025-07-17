/*
 * This file is part of the OASIS Project.
 * https://github.com/orgs/The-OASIS-Project/
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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * By contributing to this project, you agree to license your contributions
 * under the GPLv3 (or any later version) or any future licenses chosen by
 * the project author(s). Contributions include any modifications,
 * enhancements, or additions to the project. These contributions become
 * part of the project and are adopted by the project author(s).
 */

#define SECRET_SSID "STARKINDUSTRIES"
#define SECRET_PASS "IronMan123"

/*
 * SECURITY WARNING: Change the PMK to your own unique value!
 * Using the default key makes your ESP-Now communication vulnerable.
 * Generate a random 16-byte key for production use.
 *
 * This has to match the key in the SPARK code!
 *
 * Example command to generate a random key:
 * openssl rand -hex 16
 */
#define SECRET_PMK { \
  0x54, 0x68, 0x69, 0x73, 0x49, 0x73, 0x41, 0x53, \
  0x68, 0x61, 0x72, 0x65, 0x64, 0x4B, 0x65, 0x79  \
}
