import React from 'react';
import { Link } from "react-router-dom";
import waitanim from './833.gif'
import BrailleTranslatorFactory from '../modules/BrailleTranslatorFactory';
import BraillePaginator from '../modules/BraillePaginator';
import BrailleToGeometry from '../modules/BrailleToGeometry';
import GeomToGCode from '../modules/GeomToGCode';
import libLouis from '../WrapLibLouisReact';
import createModule from "../liblouisreact.mjs"; // eslint-disable-line
import menulogo from '../menu.svg'
import '../App.css';

function  braille_info (fname, desc, lang, region, flags) {
    this.fname =fname;
    this.desc = desc;
    this.lang = lang;
    this.region = region;
    this.flags = flags;
  }
  

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
          gcode:[],
          brailleinfo:[],
          brailletbl:3,
          laststatus:"",
          param:false
        };
        this.liblouis = new libLouis ();
        this.handleChange = this.handleChange.bind(this);
        
        this.handleprintws = this.handleprintws.bind(this);
        this.printws_automat = this.printws_automat.bind(this);
        this.CheckTimeout = this.CheckTimeout.bind (this);
        this.handleChangeBraille = this.handleChangeBraille.bind(this);
        this.handleClikParam = this.handleClikParam.bind(this);

        this.onopenws = this.onopenws.bind(this);
        this.onmessagews = this.onmessagews.bind(this);
        this.onclosews = this.onclosews.bind(this);

        this.realidx = 0;
        this.pending = false;
        this.lastws_event = performance.now();
        this.lastws_check =performance.now ();
    }
    
    componentDidMount ()
    {
      
      createModule().then((Module) => {
        // need to use callback form (() => function) to ensure that `add` is set to the function
        // if you use setX(myModule.cwrap(...)) then React will try to set newX = myModule.cwrap(currentX), which is wrong
        
        this.liblouis.init (Module);
        
        if (this.liblouis.isInit())
        {
          let nb = this.liblouis.get_table_nbr();
          console.log ("find " + nb.toString() + " Braille tables")
       
          let brtable = [];
          let louis = this.liblouis;
          let nbr = louis.get_table_nbr();
          for (let i = 0; i < nbr; i++)
          {
            let description = louis.get_table_description(i);
            
            //console.log (description + " " + typeof(flags) + " " + flags.toString(16));
            let br = new braille_info(
              louis.get_table_fname(i), 
              description,
              louis.get_table_lang(i), 
              louis.get_table_region(i),
              louis.get_table_flags (i)
            );
            brtable.push (
              br
            );
            //console.log (this.props.glouis().get_table_fname(i));
          }
          this.setState({brailleinfo:brtable})
       
        }
      }).catch ((error)=> {
        console.log("catch:" + error.toString());
      });
      
    }
    CheckTimeout()
    {
        if (this.state.PrintWSInProgress)
        {
            if (performance.now () - this.lastws_check > 1000)
            {
                if (this.ws)
                {
                    this.ws.send(JSON.stringify ({status:"query"}));
                    this.lastws_check = performance.now ();
                }
            }
            if (performance.now () - this.lastws_event > 30000)
            {
                if (this.ws)
                {
                    console.log ("close in timeout Checktimeout");
                    this.ws.close ();
                }
            }
        }
    }

    
    buildgcode ()    
    {
        if (! this.liblouis.isInit())
            return;
        // build Braille GCODE for current page
        let f = new BrailleTranslatorFactory();
        let Braille = f.getTranslator("LOUIS", this.liblouis, this.state.brailletbl);
        Braille.setSrc(this.state.txt);
        Braille.translate();
        let geom = new BrailleToGeometry();
        let paginator = new BraillePaginator();
        
        paginator.setcols(this.props.options.nbcol);
        paginator.setrow(this.props.options.nbline);
        paginator.setSrcLines (Braille.getBrailleLines());
        paginator.Update();
        
        geom.setPaddingY (Braille.getLinePadding ());
        //console.log ("padding " + this.Braille.getLinePadding ());
        let ptcloud = geom.BraillePageToGeom(paginator.getPage(0), 1, 1);
        
        let gcoder = new GeomToGCode();
        gcoder.GeomToGCode(ptcloud);
        let gcode = gcoder.GetGcode();

        let gcodelines = gcode.split (/\r?\n/);
        gcodelines = gcodelines.slice(0,-1);
       
        
        this.setState({gcode:gcodelines});
        this.setState({laststatus:""});
    }
   
    handleprintws (event)
    {
        if (this.state.PrintWSInProgress)
            return;

        // trasnlate text to Braille an build gcode
        this.buildgcode();

        // initialized printing
        this.setState ({PrintIdx:0});
        this.setState ({PrintWSInProgress:true});
        this.setState ({lastack:""});

        this.print_state = 0;
        this.realidx = 0;

        // connect websocket to send gcode
        this.ws = new WebSocket("ws://" + window.location.host + "/ws");
        
        
        this.ws.ref = this;
        this.ws.onopen = this.onopenws;
        this.ws.onmessage = this.onmessagews;
        this.ws.onclose = this.onclosews;
        this.lastws_event = performance.now();
        this.lastws_check =performance.now ();

        // init timer for periodic check
        this.timer = setInterval(() => {
            this.CheckTimeout();
          }, 500);
    }

    printws_automat (data)
    {
        
        this.lastws_event = performance.now();
        
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
                    console.log ("wait end of print");
                    this.print_state = 2;
                    this.setState({lastreq:"Wait end of print"});
                    break;
                }
                if (data.hasOwnProperty("free"))
                {
                    console.log ("property free :" + data["free"]);
                    if (data["free"] >= 24)
                    {
                        // send gcode to device
                        let cmd = JSON.stringify ({cmd:this.state.gcode[this.realidx] +"\r\n", id:this.realidx})
                        console.log ("sending gcode " + cmd);
                        this.ws.send(cmd);

                        // display status
                        this.setState({lastreq:this.state.gcode[this.realidx]});
                        this.realidx = this.realidx + 1;
                        this.setState ({PrintIdx:this.realidx}); // next gcode
                        this.lastws_check =performance.now ();
                    }
                }
                if (data.hasOwnProperty("id") && data.hasOwnProperty("status"))
                {
                    console.log ("next gcode");
                    this.setState ({PrintedIdx:data["id"]}); // next gcode
                    this.setState({lastack:data["status"]});
                }
                if (data.hasOwnProperty("status"))
                {
                    console.log ("receive status : " + JSON.stringify(data));
                    this.setState ({laststatus:data["status"]});
                    if (data["status"] === "ERROR")
                    {
                        console.log ("close socker for status error:" + JSON.stringify(data));
                        this.ws.close();
                    }
                }
                
                break;                    
            case 2:
                // wait end of print
                if (data.hasOwnProperty("id") && data.hasOwnProperty("status"))
                {
                    this.setState ({PrintedIdx:data["id"]}); // next gcode
                    console.log ("received status:" + JSON.stringify(data));                    
                    
                    if (data["id"] >= this.state.gcode.length -1 && data["status"] === "OK")
                    {
                        this.setState ({laststatus:"End of print"});
                        this.print_state = 3;
                        let cmd = JSON.stringify ({ctrl:"end", id:this.state.gcode.length -1})
                        console.log ("sending gcode " + cmd);
                        this.ws.send(cmd);
                        this.ws.close ();
                        //

                    }

                    if (data["status"] === "ERROR"){
                        console.log ("close socker for error:" + JSON.stringify(data));                    
                        this.ws.close();
                    }
                    
                }

                
                break;   
                
            case 3:
                console.log ("close socket for id:" + data["id"] + " " + this.state.gcode.length -1);                    
                //this.ws.close();
        }
        
    }
    onopenws (evt)
    {
        console.log ("sock connected")
              
        this.printws_automat({"start":"starting"});
    }
    onmessagews (evt)
    {
        //console.log (evt.data)
        const message = evt.data;
        console.log("ws event " + message);
        
        try{
            let data = JSON.parse(message);
            console.log("printws_automat " + JSON.stringify(data));
            this.printws_automat(data);          
        } catch (error)
        {
            console.log (error);
            this.close();   // close socket
        }

        
    }
    onclosews (evt)
    {
        console.log ("sock closed")
        this.setState({PrintWSInProgress:false});
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
    handleChangeBraille(event)
    {
      
        this.setState({brailletbl:event.target.value});  
    }
    handleClikParam (event)
    {
        event.preventDefault();
        
        
        let state = this.state.param;
        if  (state)
            this.setState({param:false});
        else
            this.setState({param:true});
        
    }
    render_print_button ()
    {
        if (this.state.brailleinfo.length === 0)
        {
            return (
                <div className='imgcontainer'>
                    <p aria-hidden='true'>Loading ... </p>
                    <img className='imgload'  src={waitanim} alt="" />
                </div>
            );
        }
        else
        {
            return (
            <button onClick={this.handleprintws}>Print</button>
            );
        }
    }
    render_braille_lang ()
    {
      if (this.state.brailleinfo.length === 0)
      {
        return (
            <div className='imgcontainer'>
                <p aria-hidden='true'>Loading ... </p>
                <img className='imgload'  src={waitanim} alt="" />
            </div>
        )
      }
      let selectedtable ="vide";
      if (this.state.brailletbl < this.state.brailleinfo.length)
        selectedtable = this.state.brailleinfo[this.state.brailletbl].desc;
      return (
        <>
        
        <label htmlFor='combobraille'>
          Braille translation
        </label>
        <select className='selectbraille' 
            onChange={this.handleChangeBraille}  
            value={this.state.brailletbl} 
            name="combobraille"
            id="combobraille"
        >
        
        {this.state.brailleinfo.map ((item, index)=> {
                 if (index === this.props.options.brailletbl)
                   return (<option  aria-selected='true' key={index} value={index}>{item.lang + " - " + item.desc }</option>);
                 else
                   return (<option  aria-selected='false' key={index} value={index}>{item.lang + " - " + item.desc }</option>);
              })
             }
                   
        </select>

        </>
       );

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
        else if (this.state.param)
        {
            return (
                <>
                <h1>Paramètres</h1>
                <nav>
                    <button 
                        onClick={this.handleClikParam}>

                    <img src={menulogo} className="menu" alt="menu" />
                    </button>
                </nav>
                {this.render_braille_lang()}

                </>
            );
        }
        else
            return (
                <>
                <h1>BrailleRAP</h1>
                <nav>
                <button onClick={this.handleClikParam}>
                    <img src={menulogo} className="menu" alt="menu" />
                </button>

                </nav>
            <textarea  
                        value={this.state.txt} 
                        onChange={this.handleChange} 
                        rows={nlines} 
                        cols={ncols} 
                        
                        >{this.state.txt}</textarea>
                
                {this.render_print_button()}
                
                <p>{this.state.laststatus}</p>
                </>
            );
    }
}

export default TextInput;