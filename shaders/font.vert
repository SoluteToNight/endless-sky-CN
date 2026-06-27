/* font.vert
Copyright (c) 2014-2020 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

// scale maps pixel coordinates to GL coordinates (-1 to 1).
uniform vec2 scale;

// Inputs from the VBO.
in vec2 vert;
in vec2 texCoordIn;

// Output to the fragment shader.
out vec2 texCoord;

// Pass through vertex position and texture coordinates.
void main() {
	texCoord = texCoordIn;
	gl_Position = vec4(vert * scale, 0.0, 1.0);
}