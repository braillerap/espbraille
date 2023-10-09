import React, { Component } from "react";
import { BrowserRouter, Routes, Route } from 'react-router-dom';

import AppOption from "./pages/components/AppOption";
import TextInput from "./pages/textinput";
import Layout from './Layout'


import './App.css';




class App extends Component {
  constructor(props)
  {
        super(props);
        this.state= (
          {
              
              srctxt : '',
              options : AppOption,
              nbline: 20,
              nbcol:24,
              
          }
      );
      

      this.SetText = this.SetText.bind(this);
      this.SetNbLine = this.SetNbLine.bind(this);
      this.SetNbCol = this.SetNbCol.bind(this);
      
      this.SetOption = this.SetOption.bind(this);
  }
 
  
  SetText (str)
  {
    this.setState ({srctxt :str});
  }
  SetNbLine (nbline)
  {
    this.setState ({nbline :nbline});
  }
  SetNbCol (nbcol)
  {
    this.setState ({nbcol:nbcol});
  }
  SetOption (opt)
  {
    this.setState ({option:opt});
    
  }
  
  render ()
  {
    return (
      <BrowserRouter>
      <Routes >
        <Route path="/" element={<Layout />}>
          <Route index element={<TextInput src={this.state.srctxt} textcb={this.SetText} options={this.state.options} /> } />
         
          <Route path="*" element={<TextInput  src={this.state.srctxt} textcb={this.SetText} options={this.state.options}/>} />
        </Route>
      </Routes>
    </BrowserRouter>
    );
  }
}

export default App;


