set breakpoint pending on
exec-file /home/kks/omnetpp/bin/opp_run
#set args -l /home/kks/tools/omnetpp/inet-hnrl/src/inet -n /home/kks/tools/omnetpp/inet-hnrl/examples:/home/kks/tools/omnetpp/inet-hnrl/src -f Test.ini -u Cmdenv -c N1_n1_nh1_nf0_nv0_tbf -r 0 
set args -l /home/kks/tools/omnetpp/inet-hnrl/src/inet -n /home/kks/tools/omnetpp/inet-hnrl/examples:/home/kks/tools/omnetpp/inet-hnrl/src -f Test.ini -u Cmdenv -c N1_n1_nh1_nf0_nv0 -r 0 
tbreak main
tbreak PercentileRecorder::finish
#tbreak UDP::bind
#tbreak EtherMACBase::calculateParameters
#tbreak HttpClientApp::initialize
#tbreak DropTailTBFQueue::initialize
#tbreak DropTailTBFQueue::handleMessag
#break DropTailTBFQueue::requestPacket
#break DropTailTBFQueue::isConformed
#break DropTailTBFQueue::scheduleTransmission
#break DropTailQueue::sendOut
#break networklayer/queue/DropTailTBFQueue.cc:64
#display state->snd_nxt
#display state->snd_una
#display state->snd_mss
#display state->snd_max
#display bytes
run
