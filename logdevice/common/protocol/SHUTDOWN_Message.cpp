/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */
#include "logdevice/common/protocol/SHUTDOWN_Message.h"
#include "logdevice/common/Processor.h"
#include "logdevice/common/Sender.h"
#include "logdevice/common/Worker.h"
#include "logdevice/common/RebuildingTypes.h"

namespace facebook { namespace logdevice {

SHUTDOWN_Message::SHUTDOWN_Message(const SHUTDOWN_Header& header)
    : Message(MessageType::SHUTDOWN, TrafficClass::FAILURE_DETECTOR),
      header_(header) {}

void SHUTDOWN_Message::serialize(ProtocolWriter& writer) const {
  writer.write(&header_, SHUTDOWN_Header::getExpectedSize(writer.proto()));
}

MessageReadResult SHUTDOWN_Message::deserialize(ProtocolReader& reader) {
  const auto proto = reader.proto();

  SHUTDOWN_Header hdr;
  hdr.serverInstanceId = ServerInstanceId_INVALID;
  reader.read(&hdr, SHUTDOWN_Header::getExpectedSize(proto));

  auto m = std::make_unique<SHUTDOWN_Message>(hdr);
  return reader.resultMsg(std::move(m));
}

Message::Disposition SHUTDOWN_Message::onReceived(const Address& from) {
  ld_debug(
      "Received SHUTDOWN Message from %s", from.id_.node_.toString().c_str());

  if (from.isClientAddress()) {
    ld_error("PROTOCOL ERROR: got a SHUTDOWN message from %s - a "
             "client address. Ignoring",
             Sender::describeConnection(from).c_str());
    err = E::PROTO;
    return Disposition::ERROR;
  }

  Worker* w = Worker::onThisThread();
  w->sender().setPeerShuttingDown(from.asNodeID());

  // Inform the LogRebuilding state machines about graceful
  // shutdown
  for (const auto& lr : w->runningLogRebuildings().map) {
    lr.second->onGracefulShutdown(
        from.asNodeID().index(), header_.serverInstanceId);
  }
  return Disposition::NORMAL;
}

size_t SHUTDOWN_Header::getExpectedSize(uint16_t proto) {
  if (proto < Compatibility::REBUILDING_WITHOUT_WAL_2) {
    return offsetof(SHUTDOWN_Header, serverInstanceId);
  } else {
    return sizeof(SHUTDOWN_Header);
  }
}

}} // namespace facebook::logdevice
