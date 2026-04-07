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
 * MQTT credentials and broker configuration.
 * Leave SECRET_MQTT_USERNAME empty ("") to connect without authentication.
 * Set SECRET_MQTT_BROKER to "" to auto-discover via WiFi gateway IP.
 *
 * For full MQTT security setup (broker, certs, all OASIS components), see:
 * https://github.com/The-OASIS-Project/dawn/blob/main/docs/MQTT_SETUP.md
 */
#define SECRET_MQTT_BROKER ""
#define SECRET_MQTT_USERNAME ""
#define SECRET_MQTT_PASSWORD ""

/*
 * MQTT TLS CA certificate (PEM format).
 * Paste the contents of your CA certificate here to enable TLS.
 * Leave empty ("") to connect without TLS.
 *
 * To get the certificate content:
 *   cat /etc/mosquitto/certs/ca.crt
 *
 * Then paste it between the quotes below, with \n at the end of each line.
 */
#define SECRET_MQTT_CA_CERT ""

/* Example with a real certificate:
#define SECRET_MQTT_CA_CERT \
  "-----BEGIN CERTIFICATE-----\n" \
  "MIIDazCCAlOgAwIBAgIUYDO...\n" \
  "...\n" \
  "-----END CERTIFICATE-----\n"
*/

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
