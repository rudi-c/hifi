#include <iostream>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/time.h>
#include <pthread.h>
#include <errno.h>
#include <fstream>
#include <limits>
#include <AgentList.h>
#include <SharedUtil.h>
#include <StdDev.h>
#include "AudioRingBuffer.h"

const unsigned short MIXER_LISTEN_PORT = 55443;

const float SAMPLE_RATE = 22050.0;

const short JITTER_BUFFER_MSECS = 2;
const short JITTER_BUFFER_SAMPLES = JITTER_BUFFER_MSECS * (SAMPLE_RATE / 1000.0);

const int BUFFER_LENGTH_BYTES = 1024;
const int BUFFER_LENGTH_SAMPLES_PER_CHANNEL = (BUFFER_LENGTH_BYTES / 2) / sizeof(int16_t);

const short RING_BUFFER_FRAMES = 10;
const short RING_BUFFER_SAMPLES = RING_BUFFER_FRAMES * BUFFER_LENGTH_SAMPLES_PER_CHANNEL;

const float BUFFER_SEND_INTERVAL_USECS = (BUFFER_LENGTH_SAMPLES_PER_CHANNEL / SAMPLE_RATE) * 1000000;

const long MAX_SAMPLE_VALUE = std::numeric_limits<int16_t>::max();
const long MIN_SAMPLE_VALUE = std::numeric_limits<int16_t>::min();

const float DISTANCE_RATIO = 3.0/4.2;
const int PHASE_DELAY_AT_90 = 20;

const int AGENT_LOOPBACK_MODIFIER = 307;

const int LOOPBACK_SANITY_CHECK = 0;

char DOMAIN_HOSTNAME[] = "highfidelity.below92.com";
char DOMAIN_IP[100] = "";    //  IP Address will be re-set by lookup on startup
const int DOMAINSERVER_PORT = 40102; 

AgentList agentList(MIXER_LISTEN_PORT);
StDev stdev;

void plateauAdditionOfSamples(int16_t &mixSample, int16_t sampleToAdd) {
    long sumSample = sampleToAdd + mixSample;
    
    long normalizedSample = std::min(MAX_SAMPLE_VALUE, sumSample);
    normalizedSample = std::max(MIN_SAMPLE_VALUE, sumSample);
    
    mixSample = normalizedSample;    
}

void *sendBuffer(void *args)
{
    int sentBytes;
    int nextFrame = 0;
    timeval startTime;
    
    gettimeofday(&startTime, NULL);

    while (true) {
        sentBytes = 0;
        
        for (int i = 0; i < agentList.getAgents().size(); i++) {
            AudioRingBuffer *agentBuffer = (AudioRingBuffer *) agentList.getAgents()[i].getLinkedData();
            
            if (agentBuffer != NULL && agentBuffer->getEndOfLastWrite() != NULL) {
                
                if (!agentBuffer->isStarted()
                    && agentBuffer->diffLastWriteNextOutput() <= BUFFER_LENGTH_SAMPLES_PER_CHANNEL + JITTER_BUFFER_SAMPLES) {
                    printf("Held back buffer %d.\n", i);
                } else if (agentBuffer->diffLastWriteNextOutput() < BUFFER_LENGTH_SAMPLES_PER_CHANNEL) {
                    printf("Buffer %d starved.\n", i);
                    agentBuffer->setStarted(false);
                } else {
                    
                    // check if we have more than we need to play out
                    int thresholdFrames = ceilf((BUFFER_LENGTH_SAMPLES_PER_CHANNEL + JITTER_BUFFER_SAMPLES) / (float)BUFFER_LENGTH_SAMPLES_PER_CHANNEL);
                    int thresholdSamples = thresholdFrames * BUFFER_LENGTH_SAMPLES_PER_CHANNEL;
                    
                    if (agentBuffer->diffLastWriteNextOutput() > thresholdSamples) {
                        // we need to push the next output forwards
                        int samplesToPush = agentBuffer->diffLastWriteNextOutput() - thresholdSamples;
                        
                        if (agentBuffer->getNextOutput() + samplesToPush > agentBuffer->getBuffer()) {
                            agentBuffer->setNextOutput(agentBuffer->getBuffer() + (samplesToPush - (agentBuffer->getBuffer() + RING_BUFFER_SAMPLES - agentBuffer->getNextOutput())));
                        } else {
                            agentBuffer->setNextOutput(agentBuffer->getNextOutput() + samplesToPush);
                        }
                    }
                    
                    // good buffer, add this to the mix
                    agentBuffer->setStarted(true);
                    agentBuffer->setAddedToMix(true);
                }
            }
        }
        
        int numAgents = agentList.getAgents().size();
        float distanceCoeffs[numAgents][numAgents];
        memset(distanceCoeffs, 0, sizeof(distanceCoeffs));

        for (int i = 0; i < agentList.getAgents().size(); i++) {
            Agent *agent = &agentList.getAgents()[i];
            
            AudioRingBuffer *agentRingBuffer = (AudioRingBuffer *) agent->getLinkedData();
            float agentBearing = agentRingBuffer->getBearing();
            bool agentWantsLoopback = false;
            
            if (agentBearing > 180 || agentBearing < -180) {
                // we were passed an invalid bearing because this agent wants loopback (pressed the H key)
                agentWantsLoopback = true;
                
                // correct the bearing
                agentBearing = agentBearing > 0 ? agentBearing - AGENT_LOOPBACK_MODIFIER : agentBearing + AGENT_LOOPBACK_MODIFIER;
            }
            
            int16_t clientMix[BUFFER_LENGTH_SAMPLES_PER_CHANNEL * 2] = {};
            
            
            for (int j = 0; j < agentList.getAgents().size(); j++) {
                if (i != j || ( i == j && agentWantsLoopback)) {
                    AudioRingBuffer *otherAgentBuffer = (AudioRingBuffer *)agentList.getAgents()[j].getLinkedData();
                    
                    float *agentPosition = agentRingBuffer->getPosition();
                    float *otherAgentPosition = otherAgentBuffer->getPosition();
                   
                    // calculate the distance to the other agent
                    
                    // use the distance to the other agent to calculate the change in volume for this frame
                    int lowAgentIndex = std::min(i, j);
                    int highAgentIndex = std::max(i, j);
                    
                    if (distanceCoeffs[lowAgentIndex][highAgentIndex] == 0) {
                        float distanceToAgent = sqrtf(powf(agentPosition[0] - otherAgentPosition[0], 2) +
                                                      powf(agentPosition[1] - otherAgentPosition[1], 2) +
                                                      powf(agentPosition[2] - otherAgentPosition[2], 2));
                        
                        distanceCoeffs[lowAgentIndex][highAgentIndex] = std::min(1.0f, powf(0.5, (logf(DISTANCE_RATIO * distanceToAgent) / logf(3)) - 1));
                    }
                    
                    
                    // get the angle from the right-angle triangle
                    float triangleAngle = atan2f(fabsf(agentPosition[2] - otherAgentPosition[2]), fabsf(agentPosition[0] - otherAgentPosition[0])) * (180 / M_PI);
                    float angleToSource;
                    
                    if (agentWantsLoopback) {
                        
                    }
                    
                    // find the angle we need for calculation based on the orientation of the triangle
                    if (otherAgentPosition[0] > agentPosition[0]) {
                        if (otherAgentPosition[2] > agentPosition[2]) {
                            angleToSource = -90 + triangleAngle - agentBearing;
                        } else {
                            angleToSource = -90 - triangleAngle - agentBearing;
                        }
                    } else {
                        if (otherAgentPosition[2] > agentPosition[2]) {
                            angleToSource = 90 - triangleAngle - agentBearing;
                        } else {
                            angleToSource = 90 + triangleAngle - agentBearing;
                        }
                    }
                    
                    if (angleToSource > 180) {
                        angleToSource -= 360;
                    } else if (angleToSource < -180) {
                        angleToSource += 360;
                    }
                    
                    angleToSource *= (M_PI / 180);
                    
                    float sinRatio = fabsf(sinf(angleToSource));
                    int numSamplesDelay = PHASE_DELAY_AT_90 * sinRatio;
                    
                    int16_t *goodChannel = angleToSource > 0  ? clientMix + BUFFER_LENGTH_SAMPLES_PER_CHANNEL : clientMix;
                    int16_t *delayedChannel = angleToSource > 0 ? clientMix : clientMix + BUFFER_LENGTH_SAMPLES_PER_CHANNEL;
                    
                    int16_t *delaySamplePointer = otherAgentBuffer->getNextOutput() == otherAgentBuffer->getBuffer()
                        ? otherAgentBuffer->getBuffer() + RING_BUFFER_SAMPLES - numSamplesDelay
                        : otherAgentBuffer->getNextOutput() - numSamplesDelay;
                    
                    
                    for (int s = 0; s < BUFFER_LENGTH_SAMPLES_PER_CHANNEL; s++) {
                        
                        if (s < numSamplesDelay) {
                            // pull the earlier sample for the delayed channel
                            
                            int earlierSample = delaySamplePointer[s] * distanceCoeffs[lowAgentIndex][highAgentIndex];
                            plateauAdditionOfSamples(delayedChannel[s], earlierSample);
                        }
                        
                        int16_t currentSample = (otherAgentBuffer->getNextOutput()[s] * distanceCoeffs[lowAgentIndex][highAgentIndex]);
                        plateauAdditionOfSamples(goodChannel[s], currentSample);
                        
                        if (s + numSamplesDelay < BUFFER_LENGTH_SAMPLES_PER_CHANNEL) {
                            plateauAdditionOfSamples(delayedChannel[s + numSamplesDelay], currentSample);
                        }
                    }
                }
            }
            
            agentList.getAgentSocket().send(agent->getPublicSocket(), clientMix, BUFFER_LENGTH_BYTES);
        }
        
        for (int i = 0; i < agentList.getAgents().size(); i++) {
            AudioRingBuffer *agentBuffer = (AudioRingBuffer *)agentList.getAgents()[i].getLinkedData();
            if (agentBuffer->wasAddedToMix()) {
                agentBuffer->setNextOutput(agentBuffer->getNextOutput() + BUFFER_LENGTH_SAMPLES_PER_CHANNEL);
                
                if (agentBuffer->getNextOutput() >= agentBuffer->getBuffer() + RING_BUFFER_SAMPLES) {
                    agentBuffer->setNextOutput(agentBuffer->getBuffer());
                }
                
                agentBuffer->setAddedToMix(false);
            }
        }
        
        double usecToSleep = usecTimestamp(&startTime) + (++nextFrame * BUFFER_SEND_INTERVAL_USECS) - usecTimestampNow();
        
        if (usecToSleep > 0) {
            usleep(usecToSleep);
        } else {
            std::cout << "Took too much time, not sleeping!\n";
        }
    }  

    pthread_exit(0);  
}

void *reportAliveToDS(void *args) {
    
    timeval lastSend;
    unsigned char output[7];
   
    while (true) {
        gettimeofday(&lastSend, NULL);
        
        *output = 'M';
        packSocket(output + 1, 895283510, htons(MIXER_LISTEN_PORT));
//        packSocket(output + 1, 788637888, htons(MIXER_LISTEN_PORT));
        agentList.getAgentSocket().send(DOMAIN_IP, DOMAINSERVER_PORT, output, 7);
        
        double usecToSleep = 1000000 - (usecTimestampNow() - usecTimestamp(&lastSend));
        
        if (usecToSleep > 0) {
            usleep(usecToSleep);
        } else {
            std::cout << "No sleep required!";
        }
    }    
}

void attachNewBufferToAgent(Agent *newAgent) {
    if (newAgent->getLinkedData() == NULL) {
        newAgent->setLinkedData(new AudioRingBuffer(RING_BUFFER_SAMPLES, BUFFER_LENGTH_SAMPLES_PER_CHANNEL));
    }
}

int main(int argc, const char * argv[])
{
    ssize_t receivedBytes = 0;
    
    agentList.linkedDataCreateCallback = attachNewBufferToAgent;
    
    agentList.startSilentAgentRemovalThread();
    
    //  Lookup the IP address of things we have hostnames
    if (atoi(DOMAIN_IP) == 0) {
        struct hostent* pHostInfo;
        if ((pHostInfo = gethostbyname(DOMAIN_HOSTNAME)) != NULL) {
            sockaddr_in tempAddress;
            memcpy(&tempAddress.sin_addr, pHostInfo->h_addr_list[0], pHostInfo->h_length);
            strcpy(DOMAIN_IP, inet_ntoa(tempAddress.sin_addr));
            printf("Domain server %s: %s\n", DOMAIN_HOSTNAME, DOMAIN_IP);
            
        } else {
            printf("Failed lookup domainserver\n");
        }
    } else {
        printf("Using static domainserver IP: %s\n", DOMAIN_IP);
    }
    
    // setup the agentSocket to report to domain server
    pthread_t reportAliveThread;
    pthread_create(&reportAliveThread, NULL, reportAliveToDS, NULL);

    unsigned char *packetData = new unsigned char[MAX_PACKET_SIZE];

    pthread_t sendBufferThread;
    pthread_create(&sendBufferThread, NULL, sendBuffer, NULL);
    
    int16_t *loopbackAudioPacket;
    if (LOOPBACK_SANITY_CHECK) {
        loopbackAudioPacket = new int16_t[1024];
    }
    
    sockaddr *agentAddress = new sockaddr;
    timeval lastReceive;
    gettimeofday(&lastReceive, NULL);
    
    bool firstSample = true;

    while (true) {
        if(agentList.getAgentSocket().receive(agentAddress, packetData, &receivedBytes)) {
            if (packetData[0] == 'I') {
                                
                //  Compute and report standard deviation for jitter calculation
                if (firstSample) {
                    stdev.reset();
                    firstSample = false;
                } else {
                    double tDiff = (usecTimestampNow() - usecTimestamp(&lastReceive)) / 1000;
                    stdev.addValue(tDiff);
                    
                    if (stdev.getSamples() > 500) {
                        printf("Avg: %4.2f, Stdev: %4.2f\n", stdev.getAverage(), stdev.getStDev());
                        stdev.reset();
                    }
                }
                
                gettimeofday(&lastReceive, NULL);
                
                // add or update the existing interface agent
                if (!LOOPBACK_SANITY_CHECK) {
                    agentList.addOrUpdateAgent(agentAddress, agentAddress, packetData[0]);
                    agentList.updateAgentWithData(agentAddress, (void *)packetData, receivedBytes);
                } else {
                    memcpy(loopbackAudioPacket, packetData + 1 + (sizeof(float) * 4), 1024);
                    agentList.getAgentSocket().send(agentAddress, loopbackAudioPacket, 1024);
                }
            }
        }
    }
    
    agentList.stopSilentAgentRemovalThread();
    pthread_join(reportAliveThread, NULL);
    pthread_join(sendBufferThread, NULL);
    
    return 0;
}

