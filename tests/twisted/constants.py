# Copyright (C) 2009 Nokia Corporation
# Copyright (C) 2009-2012 Collabora Ltd.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
# 02110-1301 USA

"""
Some handy constants for other tests to share and enjoy.
"""

from dbus import PROPERTIES_IFACE, INTROSPECTABLE_IFACE
from servicetest import tp_name_prefix, tp_path_prefix

CM = "org.freedesktop.Telepathy.ConnectionManager"

DBUS_ERROR_NO_REPLY = 'org.freedesktop.DBus.Error.NoReply'

HT_CONTACT = 1
HT_ROOM = 2

CHANNEL = tp_name_prefix + ".Channel"
CHANNEL_IFACE_DESTROYABLE = CHANNEL + ".Interface.Destroyable"
CHANNEL_IFACE_GROUP = CHANNEL + ".Interface.Group"
CHANNEL_IFACE_HOLD = CHANNEL + ".Interface.Hold"
CHANNEL_IFACE_MEDIA_SIGNALLING = CHANNEL + ".Interface.MediaSignalling"
CHANNEL_TYPE_TEXT = CHANNEL + ".Type.Text"
CHANNEL_TYPE_TUBES = CHANNEL + ".Type.Tubes"
CHANNEL_IFACE_TUBE = CHANNEL + ".Interface.Tube"
CHANNEL_TYPE_STREAM_TUBE = CHANNEL + ".Type.StreamTube"
CHANNEL_TYPE_DBUS_TUBE = CHANNEL + ".Type.DBusTube"
CHANNEL_TYPE_STREAMED_MEDIA = CHANNEL + ".Type.StreamedMedia"
CHANNEL_TYPE_TEXT = CHANNEL + ".Type.Text"

TP_AWKWARD_PROPERTIES = tp_name_prefix + ".Properties"
PROPERTY_FLAG_READ = 1
PROPERTY_FLAG_WRITE = 2

CHANNEL_TYPE = CHANNEL + '.ChannelType'
TARGET_HANDLE_TYPE = CHANNEL + '.TargetHandleType'
TARGET_HANDLE = CHANNEL + '.TargetHandle'
TARGET_ID = CHANNEL + '.TargetID'
REQUESTED = CHANNEL + '.Requested'
INITIATOR_HANDLE = CHANNEL + '.InitiatorHandle'
INITIATOR_ID = CHANNEL + '.InitiatorID'
INTERFACES = CHANNEL + '.Interfaces'

CONN = tp_name_prefix + ".Connection"
CONN_IFACE_ALIASING = CONN + '.Interface.Aliasing'
CONN_IFACE_AVATARS = CONN + '.Interface.Avatars'
CONN_IFACE_CAPS = CONN + '.Interface.Capabilities'
CONN_IFACE_CONTACTS = CONN + '.Interface.Contacts'
CONN_IFACE_CONTACT_CAPS = CONN + '.Interface.ContactCapabilities'
CONN_IFACE_REQUESTS = CONN + '.Interface.Requests'
CONN_IFACE_SIMPLE_PRESENCE = CONN + '.Interface.SimplePresence'
CONN_IFACE_POWER_SAVING = CONN + '.Interface.PowerSaving'
CONN_IFACE_SERVICE_POINT = CONN + '.Interface.ServicePoint'

CONN_STATUS_CONNECTED = 0
CONN_STATUS_CONNECTING = 1
CONN_STATUS_DISCONNECTED = 2

ATTR_CONTACT_ID = CONN + '/contact-id'
ATTR_ALIAS = CONN_IFACE_ALIASING + '/alias'
ATTR_AVATAR_TOKEN = CONN_IFACE_AVATARS + '/token'
ATTR_PRESENCE = CONN_IFACE_SIMPLE_PRESENCE + '/presence'

CONN_STATUS_REASON_NONE = 0
CONN_STATUS_REASON_REQUESTED = 1
CONN_STATUS_REASON_NETWORK_ERROR = 2

GROUP_REASON_NONE = 0
GROUP_REASON_OFFLINE = 1
GROUP_REASON_KICKED = 2
GROUP_REASON_BUSY = 3
GROUP_REASON_INVITED = 4
GROUP_REASON_BANNED = 5
GROUP_REASON_ERROR = 6
GROUP_REASON_INVALID_CONTACT = 7
GROUP_REASON_NO_ANSWER = 8
GROUP_REASON_RENAMED = 9
GROUP_REASON_PERMISSION_DENIED = 10
GROUP_REASON_SEPARATED = 11

PRESENCE_TYPE_UNSET = 0
PRESENCE_TYPE_OFFLINE = 1
PRESENCE_TYPE_AVAILABLE = 2
PRESENCE_TYPE_AWAY = 3
PRESENCE_TYPE_XA = 4
PRESENCE_TYPE_HIDDEN = 5
PRESENCE_TYPE_BUSY = 6
PRESENCE_TYPE_UNKNOWN = 7
PRESENCE_TYPE_ERROR = 8

ERROR = tp_name_prefix + '.Error'
INVALID_ARGUMENT = ERROR + '.InvalidArgument'
INVALID_HANDLE = ERROR + '.InvalidHandle'
NOT_IMPLEMENTED = ERROR + '.NotImplemented'
NOT_AVAILABLE = ERROR + '.NotAvailable'
PERMISSION_DENIED = ERROR + '.PermissionDenied'
CANCELLED = ERROR + '.Cancelled'
NOT_YOURS = ERROR + '.NotYours'
DISCONNECTED = ERROR + '.Disconnected'
NOT_CAPABLE = ERROR + '.NotCapable'

TUBE_PARAMETERS = CHANNEL_IFACE_TUBE + '.Parameters'
TUBE_STATE = CHANNEL_IFACE_TUBE + '.State'
STREAM_TUBE_SERVICE = CHANNEL_TYPE_STREAM_TUBE + '.Service'
DBUS_TUBE_SERVICE_NAME = CHANNEL_TYPE_DBUS_TUBE + '.ServiceName'
DBUS_TUBE_DBUS_NAMES = CHANNEL_TYPE_DBUS_TUBE + '.DBusNames'

TUBE_CHANNEL_STATE_LOCAL_PENDING = 0
TUBE_CHANNEL_STATE_REMOTE_PENDING = 1
TUBE_CHANNEL_STATE_OPEN = 2
TUBE_CHANNEL_STATE_NOT_OFFERED = 3

MEDIA_STREAM_TYPE_AUDIO = 0
MEDIA_STREAM_TYPE_VIDEO = 1

SOCKET_ADDRESS_TYPE_UNIX = 0
SOCKET_ADDRESS_TYPE_ABSTRACT_UNIX = 1
SOCKET_ADDRESS_TYPE_IPV4 = 2
SOCKET_ADDRESS_TYPE_IPV6 = 3

SOCKET_ACCESS_CONTROL_LOCALHOST = 0
SOCKET_ACCESS_CONTROL_PORT = 1
SOCKET_ACCESS_CONTROL_NETMASK = 2
SOCKET_ACCESS_CONTROL_CREDENTIALS = 3

TUBE_STATE_LOCAL_PENDING = 0
TUBE_STATE_REMOTE_PENDING = 1
TUBE_STATE_OPEN = 2
TUBE_STATE_NOT_OFFERED = 3

TUBE_TYPE_DBUS = 0
TUBE_TYPE_STREAM = 1

MEDIA_STREAM_DIRECTION_NONE = 0
MEDIA_STREAM_DIRECTION_SEND = 1
MEDIA_STREAM_DIRECTION_RECEIVE = 2
MEDIA_STREAM_DIRECTION_BIDIRECTIONAL = 3

MEDIA_STREAM_PENDING_LOCAL_SEND = 1
MEDIA_STREAM_PENDING_REMOTE_SEND = 2

MEDIA_STREAM_TYPE_AUDIO = 0
MEDIA_STREAM_TYPE_VIDEO = 1

MEDIA_STREAM_STATE_DISCONNECTED = 0
MEDIA_STREAM_STATE_CONNECTING = 1
MEDIA_STREAM_STATE_CONNECTED = 2

MEDIA_STREAM_DIRECTION_NONE = 0
MEDIA_STREAM_DIRECTION_SEND = 1
MEDIA_STREAM_DIRECTION_RECEIVE = 2
MEDIA_STREAM_DIRECTION_BIDIRECTIONAL = 3

SERVICE_POINT_TYPE_NONE = 0
SERVICE_POINT_TYPE_EMERGENCY = 1
SERVICE_POINT_TYPE_COUNSELING = 2

CLIENT = tp_name_prefix + '.Client'
CLIENT_PATH = tp_path_prefix + '/Client'
OBSERVER = tp_name_prefix + '.Client.Observer'
APPROVER = tp_name_prefix + '.Client.Approver'
HANDLER = tp_name_prefix + '.Client.Handler'
CLIENT_IFACE_REQUESTS = CLIENT + '.Interface.Requests'

ACCOUNT = tp_name_prefix + '.Account'
ACCOUNT_IFACE_AVATAR = ACCOUNT + '.Interface.Avatar'
ACCOUNT_IFACE_ADDRESSING = ACCOUNT + '.Interface.Addressing'
ACCOUNT_IFACE_HIDDEN = ACCOUNT + '.Interface.Hidden.DRAFT1'
ACCOUNT_IFACE_NOKIA_CONDITIONS = 'com.nokia.Account.Interface.Conditions'
ACCOUNT_PATH_PREFIX = tp_path_prefix + '/Account/'

AM = tp_name_prefix + '.AccountManager'
AM_IFACE_HIDDEN = AM + '.Interface.Hidden.DRAFT1'
AM_PATH = tp_path_prefix + '/AccountManager'

CR = tp_name_prefix + '.ChannelRequest'
CDO = tp_name_prefix + '.ChannelDispatchOperation'

CD = tp_name_prefix + '.ChannelDispatcher'
CD_IFACE_OP_LIST = tp_name_prefix + '.ChannelDispatcher.Interface.OperationList'
CD_PATH = tp_path_prefix + '/ChannelDispatcher'
CD_REDISPATCH = CD + '.Interface.Redispatch.DRAFT'

MC = tp_name_prefix + '.MissionControl5'
MC_PATH = tp_path_prefix + '/MissionControl5'

TESTDOT = "org.freedesktop.Telepathy.MC.Test."
TESTSLASH = "/org/freedesktop/Telepathy/MC/Test/"

TEST_DBUS_ACCOUNT_SERVICE = TESTDOT + "DBusAccountService"
TEST_DBUS_ACCOUNT_SERVICE_PATH = TESTSLASH + "DBusAccountService"
TEST_DBUS_ACCOUNT_SERVICE_IFACE = TEST_DBUS_ACCOUNT_SERVICE

TEST_DBUS_ACCOUNT_PLUGIN_PATH = TESTSLASH + "DBusAccountPlugin"
TEST_DBUS_ACCOUNT_PLUGIN_IFACE = TESTDOT + "DBusAccountPlugin"

PARAM_FLAG_REQUIRED = 1
PARAM_FLAG_REGISTER = 2
PARAM_FLAG_HAS_DEFAULT = 4
PARAM_FLAG_SECRET = 8
PARAM_FLAG_DBUS_PROPERTY = 16
