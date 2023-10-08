import React, { Component } from "react";
import { Outlet } from "react-router-dom";

import './App.css';

class Layout extends Component 
{
  
  
  render ()
  {
    return (
        <div className="App">
        <header className="App-header">
            
            
            <Outlet />

            
        </header>  
            
        </div>
    );
  }
}
export default Layout;