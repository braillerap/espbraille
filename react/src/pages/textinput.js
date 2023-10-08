import React from 'react';
import BrailleTranslatorFactory from '../modules/BrailleTranslatorFactory';
import BraillePaginator from '../modules/BraillePaginator';
import BrailleToGeometry from '../modules/BrailleToGeometry';
import GeomToGCode from '../modules/GeomToGCode';
import '../App.css';


class TextInput extends React.Component {
    

    constructor(props) {

        super(props);
        
        this.state = {
          txt: this.props.src,
          PrintWSInProgress: false,
          lastreq:"",
          lastack:"",
          PrintIdx: 0,
          PrintedIdx:0,
          gcode:[]
        };
        
        this.handleChange = this.handleChange.bind(this);
        
        this.handleprintws = this.handleprintws.bind(this);
        this.printws_automat = this.printws_automat.bind(this);
        this.CheckTimeout = this.CheckTimeout.bind (this);
        
        this.realidx = 0;
        this.pending = false;
        this.lastws_event = performance.now();
        
    }
   
    CheckTimeout()
    {

        if (this.state.PrintWSInProgress &&
            performance.now () - this.lastws_event > 30000)
            if (this.ws)
                this.ws.close ();
    }

    
    buildgcode ()    
    {
        // build Braille GCODE for current page
        let f = new BrailleTranslatorFactory();
        let Braille = f.getTranslator("TBFR2007", "", "");
        Braille.setSrc(this.state.txt);
        Braille.translate();
        let geom = new BrailleToGeometry();
        let paginator = new BraillePaginator();
        
        paginator.setcols(this.props.options.nbcol);
        paginator.setrow(this.props.options.nbline);
        paginator.setSrcLines (Braille.getBrailleLines());
        paginator.Update();
        console.log (Braille.getBrailleLines());
        geom.setPaddingY (Braille.getLinePadding ());
        //console.log ("padding " + this.Braille.getLinePadding ());
        let ptcloud = geom.BraillePageToGeom(paginator.getPage(0), 1, 1);
        console.log (paginator.getPage(0));
        let gcoder = new GeomToGCode();
        gcoder.GeomToGCode(ptcloud);
        let gcode = gcoder.GetGcode();

        let gcodelines = gcode.split (/\r?\n/);
        gcodelines = gcodelines.slice(0,-1);
        console.log (gcodelines);
        console.log(typeof(gcodelines));
        
        this.setState({gcode:gcodelines});
    }
   
    handleprintws (event)
    {
        if (this.state.PrintWSInProgress)
            return;

        console.log (this.state.txt);
        
        this.buildgcode();

        this.setState ({PrintIdx:0});
        this.setState ({PrintWSInProgress:true});

        this.print_state = 0;
        this.realidx = 0;
        this.ws = new WebSocket("ws://" + window.location.host + "/ws");
        
        //this.ws = new WebSocket("ws://esp32web.local/ws");
        this.ws.ref = this;
        this.ws.onopen = this.onopenws;
        this.ws.onmessage = this.onmessagews;
        this.ws.onclose = this.onclosews;
        this.lastws_event = performance.now();
        
        this.timer = setInterval(() => {
            this.CheckTimeout();
          }, 500);
        
        
    }

    printws_automat (data)
    {
        console.log(data)
        switch (this.print_state)
        {
            case 0:
                this.print_state = 1;
                this.ws.send(JSON.stringify ({cmd:"G28 X;\r\n", id:0}));
                this.setState({lastreq:"G28 X;"});
                break;
            case 1:
                if (this.realidx >= this.state.gcode.length)
                {
                    this.print_state = 2;
                    this.setState({lastreq:"Wait end of print"});
                    break;
                }
                if (data.hasOwnProperty("free"))
                {
                    console.log (data["free"]);
                    if (data["free"] > 16)
                    {
                        // send gcode to device
                        let cmd = JSON.stringify ({cmd:this.state.gcode[this.realidx] +"\r\n", id:this.realidx})
                        console.log (cmd);
                        this.ws.send(cmd);

                        // display status
                        this.setState({lastreq:this.state.gcode[this.realidx]});
                        this.realidx = this.realidx + 1;
                        this.setState ({PrintIdx:this.realidx}); // next gcode
                    }
                }
                if (data.hasOwnProperty("id") && data.hasOwnProperty("status"))
                {
                    this.setState ({PrintedIdx:data["id"]}); // next gcode
                    this.setState({lastack:data["status"]});
                }
                if (data.hasOwnProperty("status"))
                {
                    if (data["status"] == "ERROR")
                        this.ws.close();
                }
                
                break;                    
            case 2:
                // wait end of print
                if (data.hasOwnProperty("id") && data.hasOwnProperty("status"))
                {
                    this.setState ({PrintedIdx:data["id"]}); // next gcode
                                        
                    if (data["id"] >= this.state.gcode.length -1)
                    {
                        this.ws.close();

                    }
                    if (data.hasOwnProperty("status"))
                    {
                        if (data["status"] == "ERROR")
                            this.ws.close();
                    }
                    
                }

                
                break;    
        }
        
    }
    onopenws (evt)
    {
        console.log ("sock connected")
              
        this.ref.printws_automat("");
    }
    onmessagews (evt)
    {
        //console.log (evt.data)
        const message = evt.data;
        console.log(message)
        performance.now();
        try{
            let data = JSON.parse(message);
            
            this.ref.printws_automat(data);

            
        } catch (error)
        {
            console.log (error);
            this.close();   // close socket
        }

        
    }
    onclosews (evt)
    {
        console.log ("sock closed")
        this.ref.setState({PrintWSInProgress:false});
        if (this.timer)
            clearInterval(this.timer);
    }

    

    handleSubmit(event) 
    {
      event.preventDefault();
    }

    handleChange(event) 
    {
        //console.log (event.target.value)
        this.setState({txt: event.target.value});
        this.props.textcb (event.target.value);
    }
    
    render ()
    {
        const ncols = parseInt(this.props.options.nbcol);
        const nlines = parseInt(this.props.options.nbline);
        if (this.state.PrintWSInProgress)
        {
            return (
                <>
                    <p>Sending :{this.state.PrintIdx}/{this.state.gcode.length}</p>
                    <h3>{this.state.lastreq}</h3>
                    <h3>{this.state.lastack}</h3>
                    <p>Printing : {this.state.PrintedIdx}/{this.state.gcode.length}</p>
                </> 
            );       
        }
        
        else
            return (
                <>
            <textarea  
                        value={this.state.txt} 
                        onChange={this.handleChange} 
                        rows={nlines} 
                        cols={ncols} 
                        
                        >{this.state.txt}</textarea>
                
                <button onClick={this.handleprintws}>Embossage</button>
                
                </>
            );
    }
}

export default TextInput;