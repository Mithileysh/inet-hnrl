//
// Copyright (C) 2012 Kyeong Soo (Joseph) Kim
// Copyright (C) 2005 Andras Varga
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//


#include "DropTailVLANTBFQueue.h"

Define_Module(DropTailVLANTBFQueue);

DropTailVLANTBFQueue::DropTailVLANTBFQueue()
{
//    queues = NULL;
//    numQueues = NULL;
}

DropTailVLANTBFQueue::~DropTailVLANTBFQueue()
{
    for (int i=0; i<numQueues; i++)
    {
        delete queues[i];
        delete conformityTimer[i];
    }
//    delete [] queues;
}

void DropTailVLANTBFQueue::initialize()
{
    PassiveQueueBase::initialize();

    // configuration
    frameCapacity = par("frameCapacity");
    numQueues = par("numQueues");
    burstSize = par("burstSize").longValue()*8; // in bit
    meanRate = par("meanRate"); // in bps
    mtu = par("mtu").longValue()*8; // in bit
    peakRate = par("peakRate"); // in bps

    // state
    const char *classifierClass = par("classifierClass");
    classifier = check_and_cast<IQoSClassifier *>(createOne(classifierClass));
    classifier->setMaxNumQueues(numQueues);

    outGate = gate("out");

    // set subqueues
    queues.assign(numQueues, (cQueue *)NULL);
    meanBucketLength.assign(numQueues, burstSize);
    peakBucketLength.assign(numQueues, mtu);
    lastTime.assign(numQueues, simTime());
    conformityFlag.assign(numQueues, false);
    conformityTimer.assign(numQueues, (cMessage *)NULL);
    for (int i=0; i<numQueues; i++)
    {
        char buf[32];
        sprintf(buf, "queue-%d", i);
        queues[i] = new cQueue(buf);
        conformityTimer[i] = new cMessage("Conformity Timer", i);   // message kind carries a queue index
    }

    // state: RR scheduler
    currentQueueIndex = 0;

    // statistic
    numQueueReceived.assign(numQueues, 0);
    numQueueDropped.assign(numQueues, 0);
    numQueueShaped.assign(numQueues, 0);
    numQueueSent.assign(numQueues, 0);
}

void DropTailVLANTBFQueue::handleMessage(cMessage *msg)
{
    if (msg->isSelfMessage())
    {   // Conformity Timer expires
        int queueIndex = msg->getKind();    // message kind carries a queue index
        conformityFlag[queueIndex] = true;
        if (packetRequested > 0)
        {
            cMessage *msg = dequeue();
            if (msg != NULL)
            {
                packetRequested--;
                numQueueSent[queueIndex]++;
                sendOut(msg);
            }
            else
            {
                error("%s::handleMessage: Error in round-robin scheduling", getFullPath().c_str());
            }
//            requestPacket();
        }
    }
    else
    {   // a frame arrives
        int queueIndex = classifier->classifyPacket(msg);
        cQueue *queue = queues[queueIndex];
        numQueueReceived[queueIndex]++;
        int pktLength = (check_and_cast<cPacket *>(msg))->getBitLength();
// DEBUG
        ASSERT(pktLength > 0);
// DEBUG
        if (queue->isEmpty())
        {
            if (isConformed(queueIndex, pktLength))
            {
                if (packetRequested > 0)
                {
                    packetRequested--;
                    numQueueSent[queueIndex]++;
                    currentQueueIndex = queueIndex;
                    sendOut(msg);
                }
                else
                {
                    bool dropped = enqueue(msg);
                    if (dropped)
                    {
                        numQueueDropped[queueIndex]++;
                    }
                    else
                    {
                        conformityFlag[queueIndex] = true;
                    }
                }
            }
            else
            {
                numQueueShaped[queueIndex]++;
                bool dropped = enqueue(msg);
                if (dropped)
                {
                    numQueueDropped[queueIndex]++;
                }
                else
                {
                    triggerConformityTimer(queueIndex, pktLength);
                    conformityFlag[queueIndex] = false;
                }
            }
        }
        else
        {
            bool dropped = enqueue(msg);
            if (dropped)
                numQueueDropped[queueIndex]++;
        }

        if (ev.isGUI())
        {
            char buf[40];
            sprintf(buf, "q rcvd: %d\nq dropped: %d", numQueueReceived[queueIndex], numQueueDropped[queueIndex]);
            getDisplayString().setTagArg("t", 0, buf);
        }
    }
}

bool DropTailVLANTBFQueue::enqueue(cMessage *msg)
{
    int queueIndex = classifier->classifyPacket(msg);
    cQueue *queue = queues[queueIndex];

    if (frameCapacity && queue->length() >= frameCapacity)
    {
        EV << "Queue " << queueIndex << " full, dropping packet.\n";
        delete msg;
        return true;
    }
    else
    {
        queue->insert(msg);
        return false;
    }
}

cMessage *DropTailVLANTBFQueue::dequeue()
{
    bool found = false;
    int startQueueIndex = (currentQueueIndex + 1) % numQueues;  // search from the next queue for a frame to transmit
    for (int i = 0; i < numQueues; i++)
    {
       if (conformityFlag[(i+startQueueIndex)%numQueues])
       {
           currentQueueIndex = (i+startQueueIndex)%numQueues;
           found = true;
           break;
       }
    }

    if (!found)
    {
        // TO DO: further processing?
        return NULL;
    }

    cMessage *msg = (cMessage *)queues[currentQueueIndex]->pop();

    // TO DO: update statistics

    // conformity processing for the HOL frame
    if (!queues[currentQueueIndex]->isEmpty())
    {
        int pktLength = (check_and_cast<cPacket *>(queues[currentQueueIndex]->front()))->getBitLength();
        if (isConformed(currentQueueIndex, pktLength))
        {
            conformityFlag[currentQueueIndex] = true;
        }
        else
        {
            conformityFlag[currentQueueIndex] = false;
            triggerConformityTimer(currentQueueIndex, pktLength);
        }
    }
    else
    {
        conformityFlag[currentQueueIndex] = false;
    }

    // return a packet from the scheduled queue for transmission
    return (msg);
}

void DropTailVLANTBFQueue::sendOut(cMessage *msg)
{
    send(msg, outGate);
}

bool DropTailVLANTBFQueue::isConformed(int queueIndex, int pktLength)
{
    Enter_Method("isConformed()");

// DEBUG
    EV << "Last Time = " << lastTime[queueIndex] << endl;
    EV << "Current Time = " << simTime() << endl;
    EV << "Packet Length = " << pktLength << endl;
// DEBUG

    // update states
    simtime_t now = simTime();
    unsigned long long meanTemp = meanBucketLength[queueIndex] + (unsigned long long)(meanRate*(now - lastTime[queueIndex]).dbl() + 0.5);
    meanBucketLength[queueIndex] = int((meanTemp > burstSize) ? burstSize : meanTemp);
    unsigned long long peakTemp = peakBucketLength[queueIndex] + (unsigned long long)(peakRate*(now - lastTime[queueIndex]).dbl() + 0.5);
    peakBucketLength[queueIndex] = int((peakTemp > mtu) ? mtu : peakTemp);
    lastTime[queueIndex] = now;

    if (pktLength <= meanBucketLength[queueIndex])
    {
        if  (pktLength <= peakBucketLength[queueIndex])
        {
            meanBucketLength[queueIndex] -= pktLength;
            peakBucketLength[queueIndex] -= pktLength;
            return true;
        }
    }
    return false;
}

// trigger TBF conformity timer for the HOL frame in the queue,
// indicating that enough tokens will be available for its transmission
void DropTailVLANTBFQueue::triggerConformityTimer(int queueIndex, int pktLength)
{
    Enter_Method("triggerConformityCounter()");

    double meanDelay = (pktLength - meanBucketLength[queueIndex]) / meanRate;
    double peakDelay = (pktLength - peakBucketLength[queueIndex]) / peakRate;

// DEBUG
    EV << "Packet Length = " << pktLength << endl;
    dumpTbfStatus(queueIndex);
    EV << "Delay for Mean TBF = " << meanDelay << endl;
    EV << "Delay for Peak TBF = " << peakDelay << endl;
    EV << "Current Time = " << simTime() << endl;
    EV << "Counter Expiration Time = " << simTime() + max(meanDelay, peakDelay) << endl;
// DEBUG

    scheduleAt(simTime() + max(meanDelay, peakDelay), conformityTimer[queueIndex]);
}

void DropTailVLANTBFQueue::dumpTbfStatus(int queueIndex)
{
    EV << "Last Time = " << lastTime[queueIndex] << endl;
    EV << "Current Time = " << simTime() << endl;
    EV << "Token bucket for mean rate/burst control " << endl;
    EV << "- Burst size [bit]: " << burstSize << endl;
    EV << "- Mean rate [bps]: " << meanRate << endl;
    EV << "- Bucket length [bit]: " << meanBucketLength[queueIndex] << endl;
    EV << "Token bucket for peak rate/MTU control " << endl;
    EV << "- MTU [bit]: " << mtu << endl;
    EV << "- Peak rate [bps]: " << peakRate << endl;
    EV << "- Bucket length [bit]: " << peakBucketLength[queueIndex] << endl;
}

void DropTailVLANTBFQueue::finish()
{
    for (int i=0; i < numQueues; i++)
    {
        std::stringstream ss_received, ss_dropped, ss_shaped, ss_sent;
        ss_received << "packets received by per-VLAN queue[" << i << "]";
        ss_dropped << "packets dropped by per-VLAN queue[" << i << "]";
        ss_shaped << "packets shaped by per-VLAN queue[" << i << "]";
        ss_sent << "packets sent by per-VLAN queue[" << i << "]";
        recordScalar((ss_received.str()).c_str(), numQueueReceived[i]);
        recordScalar((ss_dropped.str()).c_str(), numQueueDropped[i]);
        recordScalar((ss_shaped.str()).c_str(), numQueueShaped[i]);
        recordScalar((ss_sent.str()).c_str(), numQueueSent[i]);
    }
}
