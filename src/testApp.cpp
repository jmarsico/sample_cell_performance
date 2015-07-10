#include "testApp.h"

using namespace ofxCv;
using namespace cv;

void testApp::setup() {
	ofSetLogLevel(OF_LOG_VERBOSE);
    ofSetVerticalSync(true);
	ofBackground(70);
    ofSetFrameRate( 44 );
    
    ofHideCursor();
    
    //initialize connection
    grabber = IPVideoGrabber::makeShared();
    
    //keep trying to connect until you connect
    while(grabber->isConnected()==false)
    {
        ofLog() << "connecting...";
        grabber->setCameraName("cam1");
        //grabber->setURI("http://192.168.0.10/videostream.cgi?user=admin&pwd=");
        //grabber->setURI("http://82.79.176.85:8081/axis-cgi/mjpg/video.cgi?resolution=320x240");
        grabber->setURI("http://192.168.2.2/videostream.cgi?user=admin&pwd=kelly&resolution=640x480");
//        grabber->setURI("http://216.8.159.21/axis-cgi/mjpg/video.cgi?resolution=320x240");
//        grabber->setUsername("admin");
//        grabber->setPassword("kelly");
        grabber->connect();
    } 
    
    ofLog() << "connection made!";
    ofLog() << "width: " << grabber->getWidth();
    ofLog() << "height: " << grabber->getHeight();
    
    //GUI
    gui.setup("panel");
    gui.add(bShowThresh.setup("show threshold", false));
    gui.add(bShowContour.setup("show contours", false));
    gui.add(backgroundThresh.setup("bgThresh", 21, 0, 255));
    gui.add(minArea.setup("minArea", 10,1, 100));
    gui.add(maxArea.setup("maxArea", 100, 101, 200));
    gui.add(blurAmount.setup("blur", 10, 1, 100));
    gui.add(erodeAmount.setup("erode", 0, 0, 10));
    gui.add(dilateAmount.setup("dilate", 0, 0, 10));
    gui.add(learningTime.setup("learnTime", 50, 2, 1000));
    gui.add(resetbkgnd.setup("reset background"));
    gui.add(bReady.setup("Ready for Cells", false));
    gui.add(resetCells.setup("reset cells"));
    gui.add(bLoadCells.setup("load cells"));
    gui.add(bSaveCells.setup("save cells"));
    gui.add(glassThresh.setup("glass threshold", 0, 0, 255));
    gui.add(stateChangeWait.setup("glass state limiter", 300, 10, 1000));
    gui.add(framerate.set("Frame Rate", 0));
    gui.setPosition(10, ofGetHeight() - gui.getHeight());
    gui.loadFromFile("settings.xml");
    
    
    //ofxCV setup
    thresholded.allocate(grabber->getWidth(), grabber->getHeight(), OF_IMAGE_COLOR);
	contourFinder.setMinAreaRadius(1);
    contourFinder.setMaxAreaRadius(100);
	contourFinder.setThreshold(200);
    contourFinder.setAutoThreshold(false);
	contourFinder.getTracker().setPersistence(50);   //wait for half a frame before forgetting something
	contourFinder.getTracker().setMaximumDistance(32);  // an object can move up to 32 pixels per frame
    background.setLearningTime(50);
    background.setThresholdValue(200);
    
    panelW = panelH = 100;
    vector<ofPoint> sgPoints;
    for(int x = 0; x < 3; x++)
    {
        for(int y = 0; y < 2; y++)
        {
            ofPoint p;
            p.set(x* panelW, y*panelH);
            sgPoints.push_back(p);
        }
    }
    
    
    
    //set up sample cells
    for(int i = 0; i < NUMSAMPLES; i++)
    {
        cells[i].init(i);
        brightVals.push_back(0);
        prevVals.push_back(0);
    }
    startPoint.set(10,10);
    
    
    if(pointsXML.load("cellPoints.xml"))
    {
        ofLog() << "XML loaded successfully";
        
    }
    else ofLog() << "XML did not load, check data/ folder";
    
    
    //OSC setup
    sender.setup("127.0.0.1", 12345);
    
    //DMX Setup
    //zero our DMX value array
    memset( dmxData_, 0, DMX_DATA_LENGTH );
    //open the device
    dmxInterface_ = ofxGenericDmx::openFirstDevice();
    if ( dmxInterface_ == 0 ) printf( "No Enttec Device Found\n" );
    else printf( "isOpen: %i\n", dmxInterface_->isOpen() );
    printf("ofxGenericDmx addon version: %s.%s\n", ofxGenericDmx::VERSION_MAJOR, ofxGenericDmx::VERSION_MINOR);

    bIntroLoadCells = true;
    
}


//------------------------------------------------------------------------------
void testApp::update() {
    
    framerate = (int)ofGetFrameRate();
	// update the cameras
    grabber->update();
    
    //update cv params
    contourFinder.setMinAreaRadius(minArea);
    contourFinder.setMaxAreaRadius(maxArea);
    background.setLearningTime(learningTime);
    background.setThresholdValue(backgroundThresh);
    int erodeNum = erodeAmount;
    int dilateNum = dilateAmount;
    
    //reset the background
    if(resetbkgnd) background.reset();
    
    
    //get a frame from IP cam
    grabFrame.setFromPixels(grabber->getPixels(), grabber->getWidth(), grabber->getHeight(), OF_IMAGE_COLOR);
    grabFrame.resize(645 , 485 );
    
    
    //get the thresholded frame from background function
    background.update(grabFrame, thresholded);
    thresholded.update();
    
    
    
    //manipulate thresholded image before contour finder
    dilate(thresholded, dilateNum);
    erode(thresholded, erodeNum);
    blur(thresholded, blurAmount);
    
    if(bShowContour){
        contourFinder.findContours(thresholded);
    //ofLog() << "number contours: " << contourFinder.size();
    }
    
    threshPix.setFromPixels(thresholded.getPixels(), thresholded.getWidth(), thresholded.getHeight(),OF_IMAGE_GRAYSCALE);
    
    //load cells at startup
    if(contourFinder.size() > 0 && bIntroLoadCells) {
        loadCellsFromXml();
        bIntroLoadCells = false;
    }

    
    //----------------- setting up sampling cells -------------------------
    if(bReady)
    {
        //we use different function for setting the first cell (in case we want to link)
        if(!cells[0].isPointsSet() && !cells[0].isSettingPoints())
        {
            cells[0].setPoints(threshPix, startPoint);
        }
        
        //if the first cell is set, use the second setup function
        if(cells[0].isPointsSet())
        {
            //go through each cell, starting with the second
            for(int i = 1; i < NUMSAMPLES; i++)
            {
                //if the previous cell is set.
                //if this cell is not in the process of setting points
                //if this cell is not already set up
                if(cells[i-1].isPointsSet() && !cells[i].isSettingPoints() && !cells[i].isPointsSet())
                {
                    cells[i].setPoints(threshPix, startPoint);
                }
            }
        }
    }
    
    //if a cell is set, go ahead and start getting its brightness or color
    for(int i = 0; i < NUMSAMPLES; i++)
    {
        if(cells[i].isPointsSet())
        {
            cells[i].update(threshPix);
            brightVals[i] = cells[i].getAverageBrightness(1);
        }

    
    
    }
    
    //reset and re-init cells
    if(resetCells)
    {
        for(int i = 0; i < NUMSAMPLES; i ++)
        {
            cells[i].reset();
            cells[i].init(i);
        }
        bReady = false;
    }
    
    //save or load cells
    if(bSaveCells) saveCellsToXml();
    if(bLoadCells) loadCellsFromXml();
    
    sendOscMessages();
    sendDMX();
}


//------------------------------------------------------------------------------
void testApp::draw() {
    
    RectTracker& tracker = contourFinder.getTracker();
    ofSetColor(255);
    
    ofPushMatrix();
        ofTranslate(startPoint);
    
        //draw the video from the ofImage
        if(!bShowThresh) grabFrame.draw(0,0);
    
        //draw threshold
        if(bShowThresh) thresholded.draw(0,0);
    
        //draw contours
        if(bShowContour)
        {
            ofSetColor(0, 244, 0);
            contourFinder.draw();
            for(int i = 0; i < contourFinder.size(); i++)
            {
            ofPoint center = toOf(contourFinder.getCenter(i));
            ofPushMatrix();
                ofSetColor(255,0,0);
                ofTranslate(center.x, center.y);
                int label = contourFinder.getLabel(i);
                string msg = ofToString(i) + ":" + ofToString(tracker.getAge(label));
                ofDrawBitmapString(msg, 0, 0);
                ofVec2f velocity = toOf(contourFinder.getVelocity(i));
                ofScale(5, 5);
                ofLine(0, 0, velocity.x, velocity.y);
            ofPopMatrix();
            }
        }
    ofPopMatrix();

       
    // finally, a report:
    ofSetColor(0,255,0);
    stringstream reportStr;
        reportStr
        << "num blobs found " << contourFinder.size() << endl
        << "framerate: " << ofGetFrameRate() << endl
        << "width of IP feed: " << grabber->getWidth() << endl
        << "height of IP feed: " << grabber->getHeight() << endl
        << "IP cam framerate: " << grabber->getFrameRate() << endl
        << "URL: " << grabber->getURI() << endl;
        //ofDrawBitmapString(reportStr.str(), 20, thresholded.getHeight() + 20);
    
    //draw the cells
    for(int i = 0; i < NUMSAMPLES; i++)
    {
        cells[i].draw();
        ofPushMatrix();
        ofTranslate(startPoint.x + thresholded.getWidth() + 20, startPoint.y);
        ofPopMatrix();
    }
    
    gui.draw();
    
    
}

//--------------------------------------------------------------
void testApp::loadCellsFromXml(){
    
    ofLog() << "Starting to Load cells from XML...";
    
    for(int i = 0; i < NUMSAMPLES; i++)
    {
        pointsXML.pushTag("CELL", i);
        cells[i].setPoints(threshPix, startPoint);
        
        for(int j = 0; j < 4; j++)
        {
            
            int x = pointsXML.getValue("PT:X", 0, j);
            int y = pointsXML.getValue("PT:Y", 0, j);
            
            //set it to the points of that cell and add to the polyline
            cells[i].mouseClick.x = x;
            cells[i].mouseClick.y = y;
            cells[i].addPoint();
            ofLogVerbose() << "loadFromXML: cell[" << i << "]: point.x = " << x;
            ofLogVerbose() << "loadFromXML: cell[" << i << "]: point.y = " << y;
        }
        
        cells[i].bSettingPoints = false;
        cells[i].bIsSet = true;
        cells[i].getPixLocations();
        pointsXML.popTag();
    }
}

//--------------------------------------------------------------
void testApp::saveCellsToXml(){
    pointsXML.clear();
    
    for(int i = 0; i < NUMSAMPLES; i++)
    {
        if(cells[i].isPointsSet())
        {
            //create a new cell child
            pointsXML.addTag("CELL");
            //set location to that cell child
            pointsXML.pushTag("CELL", i);
            
            //go through points
            for(int j = 0; j < cells[i].p.size(); j++)
            {
                pointsXML.addTag("PT");
                pointsXML.pushTag("PT", j);
                pointsXML.setValue("X", cells[i].p[j].point.x);
                pointsXML.setValue("Y", cells[i].p[j].point.y);
                pointsXML.popTag();
            }
            pointsXML.popTag();
        }
    }
    pointsXML.save("cellPoints.xml");
}


//--------------------------------------------------------------
void testApp::sendOscMessages(){
    
    
    for(int i = 0; i < brightVals.size(); i++)
    {
        if(prevVals[i] <=glassThresh && brightVals[i] > glassThresh)
        {
            ofxOscMessage m;
            m.setAddress("/" + ofToString(i));
            m.addIntArg(brightVals[i]);
            sender.sendMessage(m);
        }
        prevVals[i] = brightVals[i];
        
    }
}


//--------------------------------------------------------------
void testApp::sendDMX(){
    //force first byte to zero (it is not a channel but DMX type info - start code)
    dmxData_[0] = 0;
    
    for(int i = 1; i < NUMSAMPLES+1; i++)
    {
        
        
        dmxData_[i] = brightVals[i-1];
    }
    
    if ( ! dmxInterface_ || ! dmxInterface_->isOpen() ) {
        printf( "Not updating, enttec device is not open.\n");
    }
    else{
        //send the data to the dmx interface
        dmxInterface_->writeDmx( dmxData_, DMX_DATA_LENGTH );
    }
}

//--------------------------------------------------------------
void testApp::exit(){
    
    
    //zero our DMX value array
    memset( dmxData_, 0, DMX_DATA_LENGTH );
    if ( ! dmxInterface_ || ! dmxInterface_->isOpen() ) {
        printf( "Not updating, enttec device is not open.\n");
    }
    else{
        //send the data to the dmx interface
        dmxInterface_->writeDmx( dmxData_, DMX_DATA_LENGTH );
    }
    
}

//--------------------------------------------------------------
void testApp::keyPressed(int key){
    if(key == 'm') ofShowCursor();
}


//--------------------------------------------------------------
void testApp::mouseDragged(int x, int y, int button){
}

//--------------------------------------------------------------
void testApp::mousePressed(int x, int y, int button){
	
}

//--------------------------------------------------------------
void testApp::mouseMoved(int x, int y){
    ofShowCursor();
}

//--------------------------------------------------------------
void testApp::mouseReleased(int x, int y, int button){
	
}

