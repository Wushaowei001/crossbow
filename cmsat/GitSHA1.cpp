/*
 * CryptoMiniSat
 *
 * Copyright (c) 2009-2014, Mate Soos. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.0 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301  USA
*/

#define GIT_SHA1 "9d0f57c745cc731238db394d62048234d3ba917d"
#define GIT_TAG ""
static const char myversion_sha1[] = GIT_SHA1;
static const char myversion_tag[] = GIT_TAG;

const char* get_git_version_sha1()
{
    return myversion_sha1;
}

const char* get_git_version_tag()
{
    return myversion_tag;
}
